// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic, independently authored checks for the trainyard exit data
// qualifiers. Original graphs and HKXS bodies are exercised only by the
// native session QA against the owner's own data.
#include "TrainyardExit.h"
#include <cstdio>
#include <exception>
int main(){
    try{const int groups=wolf2009::RunTrainyardExitTests();
        std::printf("WOLF2009_TRAINYARD_EXIT_SYNTHETIC groups=%d failed=0\n",groups);return groups==4?0:1;}
    catch(const std::exception& error){std::fprintf(stderr,"WOLF2009_TRAINYARD_EXIT_SYNTHETIC failed reason=%s\n",error.what());return 1;}
}
