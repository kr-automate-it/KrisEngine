#include "uci.h"
#include "eval.h"

int main() {
    init_psq_tables();
    uci_loop();
    return 0;
}
