"""
lichess_uci_bot.py — Bot Lichess oparty na silniku UCI (Kris/ChessCpp).

Wymagania:
  pip install requests python-chess

Uzycie:
  python lichess_uci_bot.py --token <TOKEN> --engine ./kris_spsa_tuning.exe
  python lichess_uci_bot.py --token <TOKEN> --engine ./kris_spsa_tuning.exe --no-seek

Limit Lichess: 100 gier / 24h. Bot wysyla wyzwanie co ~15 min = max ~96/dzien.
"""

import argparse
import json
import logging
import os
import sys
import threading
import time

import requests
import chess
import chess.engine

# === Konfiguracja ===

API_BASE = "https://lichess.org/api"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler("lichess_uci_bot.log", encoding="utf-8"),
    ],
)
log = logging.getLogger("lichess_uci_bot")

# Warianty
SUPPORTED_VARIANTS = {"standard"}

# Max jednoczesnych partii
MAX_GAMES = 1  # 1 = caly CPU dla jednej partii = najlepsza jakosc

# Wyzwania co ile sekund (900s = 15 min -> ~96 wyzwan/dzien < 100 limit)
SEEK_INTERVAL = 120

# Czasy do gry — (limit_sek, increment_sek, nazwa)
SEEK_TIME_CONTROLS = [
    (180, 2, "blitz 3+2"),
    (300, 0, "blitz 5+0"),
    (300, 3, "blitz 5+3"),
    (600, 0, "rapid 10+0"),
]

# Ile gier juz rozegralismy dzisiaj (reset co 24h)
_games_today = 0
_games_today_lock = threading.Lock()
_day_start = time.time()
DAILY_GAME_LIMIT = 95  # bufor 5 gier pod limit 100


# === Lichess API client ===

class LichessClient:
    def __init__(self, token: str):
        self.session = requests.Session()
        self.session.headers.update({
            "Authorization": f"Bearer {token}",
            "Accept": "application/json",
        })

    def get(self, path: str, **kwargs):
        return self.session.get(f"{API_BASE}{path}", **kwargs)

    def post(self, path: str, **kwargs):
        return self.session.post(f"{API_BASE}{path}", **kwargs)

    def stream(self, path: str, **kwargs):
        r = self.session.get(f"{API_BASE}{path}", stream=True, **kwargs)
        r.raise_for_status()
        for raw in r.iter_lines():
            if raw:
                try:
                    yield json.loads(raw)
                except json.JSONDecodeError:
                    pass

    def account(self) -> dict:
        return self.get("/account").json()

    def upgrade_to_bot(self) -> bool:
        r = self.post("/bot/account/upgrade")
        return r.status_code == 200

    def accept_challenge(self, challenge_id: str):
        self.post(f"/challenge/{challenge_id}/accept")

    def decline_challenge(self, challenge_id: str, reason: str = "generic"):
        self.post(f"/challenge/{challenge_id}/decline", json={"reason": reason})

    def make_move(self, game_id: str, uci_move: str):
        for attempt in range(3):
            try:
                self.post(f"/bot/game/{game_id}/move/{uci_move}")
                return
            except Exception as e:
                if attempt < 2:
                    time.sleep(1)
                else:
                    raise

    def send_chat(self, game_id: str, text: str, room: str = "player"):
        self.post(f"/bot/game/{game_id}/chat", data={"room": room, "text": text})

    def abort_game(self, game_id: str):
        self.post(f"/bot/game/{game_id}/abort")

    def get_online_bots(self, nb: int = 50) -> list:
        r = None
        try:
            r = self.session.get(f"{API_BASE}/bot/online", params={"nb": nb}, stream=True)
            r.raise_for_status()
            bots = []
            for line in r.iter_lines():
                if line:
                    try:
                        bots.append(json.loads(line))
                    except json.JSONDecodeError:
                        pass
            return bots
        except Exception:
            return []
        finally:
            if r is not None:
                r.close()

    def challenge_user(self, username: str, clock_limit: int = 300,
                       clock_increment: int = 3, color: str = "random") -> bool:
        r = self.post(f"/challenge/{username}", data={
            "rated": "true",
            "clock.limit": clock_limit,
            "clock.increment": clock_increment,
            "color": color,
            "variant": "standard",
        })
        if r.status_code != 200:
            log.info("Challenge %s: HTTP %d — %s", username, r.status_code, r.text[:200])
        return r.status_code == 200


# === Licznik gier ===

def can_play() -> bool:
    global _games_today, _day_start
    with _games_today_lock:
        # Reset po 24h
        if time.time() - _day_start > 86400:
            _games_today = 0
            _day_start = time.time()
        return _games_today < DAILY_GAME_LIMIT

def count_game():
    global _games_today
    with _games_today_lock:
        _games_today += 1
        log.info("Gier dzisiaj: %d / %d", _games_today, DAILY_GAME_LIMIT)


# === Bot ===

class LichessUCIBot:
    def __init__(self, client: LichessClient, engine_path: str,
                 hash_mb: int = 64, auto_seek: bool = True):
        self.client = client
        self.engine_path = engine_path
        self.hash_mb = hash_mb
        self.auto_seek = auto_seek
        self._games: dict[str, threading.Thread] = {}
        self._lock = threading.Lock()
        self._my_username: str = ""

    def _should_accept(self, challenge: dict) -> tuple[bool, str]:
        variant = challenge.get("variant", {}).get("key", "standard")
        if variant not in SUPPORTED_VARIANTS:
            return False, "variant"

        speed = challenge.get("speed", "")
        if speed == "ultraBullet":
            return False, "tooFast"

        if not can_play():
            return False, "later"

        with self._lock:
            if len(self._games) >= MAX_GAMES:
                return False, "later"

        return True, ""

    def on_challenge(self, challenge: dict):
        cid = challenge["id"]
        name = challenge.get("challenger", {}).get("name", "?")
        ok, reason = self._should_accept(challenge)
        if ok:
            log.info("Akceptuje wyzwanie od %s (id=%s)", name, cid)
            self.client.accept_challenge(cid)
        else:
            log.info("Odrzucam wyzwanie od %s (id=%s, powod=%s)", name, cid, reason)
            self.client.decline_challenge(cid, reason)

    def on_game_start(self, game_info: dict):
        game_id = game_info["id"]
        with self._lock:
            if game_id in self._games:
                return
            t = threading.Thread(
                target=self._game_loop,
                args=(game_id,),
                name=f"game-{game_id}",
                daemon=True,
            )
            self._games[game_id] = t
        t.start()

    def _game_loop(self, game_id: str):
        log.info("Partia %s — start", game_id)

        # Uruchom silnik UCI
        try:
            engine = chess.engine.SimpleEngine.popen_uci(self.engine_path)
            engine.configure({"Hash": self.hash_mb, "OwnBook": True})
        except Exception as e:
            log.error("Nie mozna uruchomic silnika: %s", e)
            return

        try:
            board = chess.Board()
            my_color = None
            greeted = False

            for event in self.client.stream(f"/bot/game/stream/{game_id}"):
                etype = event.get("type")

                if etype == "gameFull":
                    white_id = event.get("white", {}).get("id", "").lower()
                    my_color = chess.WHITE if white_id == self._my_username else chess.BLACK

                    state = event.get("state", {})
                    board = self._apply_moves(chess.Board(), state.get("moves", ""))

                    if not greeted:
                        self.client.send_chat(game_id, "gl hf! I'm Kris, a homemade chess engine.")
                        greeted = True

                    self._maybe_move(game_id, board, my_color, state, engine)

                elif etype == "gameState":
                    board = self._apply_moves(chess.Board(), event.get("moves", ""))
                    status = event.get("status", "started")
                    if status not in ("started", "created"):
                        log.info("Partia %s zakonczona: %s", game_id, status)
                        count_game()
                        break
                    self._maybe_move(game_id, board, my_color, event, engine)

                elif etype == "chatLine":
                    pass

        except Exception as e:
            log.exception("Blad w grze %s: %s", game_id, e)
        finally:
            try:
                engine.quit()
            except Exception:
                pass
            with self._lock:
                self._games.pop(game_id, None)
            log.info("Partia %s — koniec", game_id)

    @staticmethod
    def _apply_moves(board: chess.Board, moves_str: str) -> chess.Board:
        if not moves_str:
            return board
        for uci in moves_str.split():
            try:
                board.push_uci(uci)
            except Exception:
                break
        return board

    def _maybe_move(self, game_id: str, board: chess.Board,
                    my_color, state: dict, engine):
        if my_color is None:
            return
        if board.turn != my_color:
            return
        if board.is_game_over():
            return

        # Pobierz czasy z state
        wtime = state.get("wtime", 60000) or 60000
        btime = state.get("btime", 60000) or 60000
        winc = state.get("winc", 0) or 0
        binc = state.get("binc", 0) or 0

        # Wyslij go z pelnym czasem — silnik sam zarzadza czasem
        try:
            result = engine.play(
                board,
                chess.engine.Limit(
                    white_clock=wtime / 1000.0,
                    black_clock=btime / 1000.0,
                    white_inc=winc / 1000.0,
                    black_inc=binc / 1000.0,
                ),
            )
        except Exception as e:
            log.error("Blad silnika w partii %s: %s", game_id, e)
            return

        if result.move is None:
            return

        log.info("Partia %s  ruch=%s", game_id, result.move.uci())
        self.client.make_move(game_id, result.move.uci())

    # === Seek loop — wyzwania do botow ===

    def _seek_loop(self):
        import random
        log.info("Seek loop uruchomiony (co %ds, limit %d gier/dzien)",
                 SEEK_INTERVAL, DAILY_GAME_LIMIT)
        time.sleep(60)  # poczekaj minute na start
        while True:
            try:
                if not can_play():
                    log.info("Limit gier osiagniety — czekam na reset (24h).")
                    time.sleep(3600)
                    continue

                with self._lock:
                    if len(self._games) >= MAX_GAMES:
                        log.info("Aktywna partia — pomijam seek.")
                        time.sleep(60)
                        continue

                bots = self.client.get_online_bots(nb=100)
                candidates = []
                for b in bots:
                    if b.get("username", "").lower() == self._my_username.lower():
                        continue
                    # Tylko boty 2000+
                    rating = max(b.get("perfs", {}).get("blitz", {}).get("rating", 0),
                                 b.get("perfs", {}).get("rapid", {}).get("rating", 0),
                                 b.get("perfs", {}).get("bullet", {}).get("rating", 0))
                    if rating < 2000:
                        continue
                    # Pomijaj boty ktore aktualnie graja
                    if b.get("playing", False):
                        continue
                    candidates.append(b)

                log.info("Boty online 2000+, wolne: %d", len(candidates))

                if candidates:
                    target = random.choice(candidates)
                    username = target.get("username", "")
                    limit, increment, tc_name = random.choice(SEEK_TIME_CONTROLS)

                    log.info("Wyzywam %s (%s)...", username, tc_name)
                    ok = self.client.challenge_user(
                        username, clock_limit=limit, clock_increment=increment
                    )
                    if ok:
                        log.info("Wyzwanie wyslane do %s (%s)", username, tc_name)
                    else:
                        log.info("Nie udalo sie wyzwac %s", username)
                else:
                    log.info("Brak botow online.")

            except Exception as e:
                log.warning("Blad w seek loop: %s", e)

            time.sleep(SEEK_INTERVAL)

    # === Main loop ===

    def run(self):
        try:
            self._my_username = self.client.account().get("username", "").lower()
        except Exception:
            pass
        log.info("Bot '%s' uruchomiony. Silnik: %s", self._my_username, self.engine_path)

        if self.auto_seek:
            threading.Thread(target=self._seek_loop, name="seek", daemon=True).start()

        while True:
            try:
                for event in self.client.stream("/stream/event"):
                    etype = event.get("type")
                    if etype == "challenge":
                        self.on_challenge(event["challenge"])
                    elif etype == "gameStart":
                        self.on_game_start(event["game"])
                    elif etype == "gameFinish":
                        gid = event.get("game", {}).get("id", "?")
                        log.info("Partia zakonczona: %s", gid)

            except requests.exceptions.ConnectionError as e:
                log.warning("Utracono polaczenie (%s) — ponawiam za 10s...", e)
                time.sleep(10)
            except requests.exceptions.HTTPError as e:
                log.error("HTTP error: %s", e)
                time.sleep(30)
            except KeyboardInterrupt:
                log.info("Zatrzymano przez uzytkownika.")
                break
            except Exception as e:
                log.exception("Nieoczekiwany blad: %s", e)
                time.sleep(15)


# === CLI ===

def main():
    parser = argparse.ArgumentParser(description="Lichess UCI bot (Kris)")
    parser.add_argument("--token", type=str, default=None,
                        help="Token API Lichess (lub LICHESS_TOKEN env)")
    parser.add_argument("--engine", type=str, default="./kris_spsa_tuning.exe",
                        help="Sciezka do silnika UCI")
    parser.add_argument("--hash", type=int, default=64,
                        help="Hash w MB (domyslnie 64)")
    parser.add_argument("--no-seek", action="store_true",
                        help="Wylacz automatyczne wyzwania")
    parser.add_argument("--upgrade", action="store_true",
                        help="Upgrade konta do Bot (NIEODWRACALNE!)")
    args = parser.parse_args()

    token = args.token or os.environ.get("LICHESS_TOKEN", "")
    if not token:
        print("Podaj token: --token <TOKEN>  lub ustaw LICHESS_TOKEN")
        sys.exit(1)

    client = LichessClient(token)

    try:
        account = client.account()
    except Exception as e:
        print(f"Blad polaczenia z Lichess: {e}")
        sys.exit(1)

    username = account.get("username", "?")
    is_bot = account.get("title") == "BOT"
    log.info("Zalogowano jako: %s (bot=%s)", username, is_bot)

    if args.upgrade:
        if is_bot:
            log.info("Konto jest juz botem.")
        else:
            confirm = input("UWAGA: Upgrade do Bot jest NIEODWRACALNY!\nWpisz 'TAK': ").strip()
            if confirm == "TAK":
                if client.upgrade_to_bot():
                    log.info("Upgrade udany!")
                else:
                    log.error("Upgrade nie powiodl sie.")
            else:
                log.info("Anulowano.")
        return

    if not is_bot:
        log.error("Konto '%s' nie jest botem! Uzyj --upgrade", username)
        sys.exit(1)

    bot = LichessUCIBot(client, args.engine, hash_mb=args.hash,
                        auto_seek=not args.no_seek)
    bot.run()


if __name__ == "__main__":
    main()
