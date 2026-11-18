// SPDX-License-Identifier: GPL-3.0-or-later
#include "ownedsupervisor.h"
int main(int argc, char** argv) {
    return argc > 1 ? SeamlessOwned::runSupervisor(argv + 1) : 125;
}
