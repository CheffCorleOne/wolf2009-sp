// SPDX-License-Identifier: GPL-3.0-or-later
#include "Dds.h"
#include "Entities.h"
#include "Material.h"
#include "ZipArchive.h"
#include <cstring>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::cout << "wolf2009-content --self-test\n";
        return 2;
    }
    const int dds = wolf2009::RunDdsTests();
    const int entities = wolf2009::RunEntityTests();
    const int materials = wolf2009::RunMaterialTests();
    const int zip = wolf2009::RunZipTests();
    return dds || entities || materials || zip ? 1 : 0;
}
