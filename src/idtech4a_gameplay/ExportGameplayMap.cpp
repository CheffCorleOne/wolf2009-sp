// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameplayFixture.h"
#include "MapPhysics.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void Write(const std::string &path,const std::string &text) {
    std::ofstream out(path,std::ios::binary);
    if(!out || !out.write(text.data(),static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Cannot write generated gameplay fixture");
}
}
int main(int argc,char **argv) {
    try {
        if(argc<3 || argc>4) {
            std::cerr << "wolf2009-gameplay-map --fixture OUTPUT_NO_EXTENSION\n"
                         "wolf2009-gameplay-map INPUT.mpk OUTPUT_NO_EXTENSION [START_NAME]\n";
            return 2;
        }
        wolf2009::GameplayMap map;
        if(std::string(argv[1])=="--fixture") map=wolf2009::MakeGameplayFixture();
        else {
            std::ifstream input(argv[1],std::ios::binary);
            if(!input) throw std::runtime_error("Cannot open map package");
            const auto archive=wolf2009::Archive::Scan(input);
            std::string name;
            for(const auto &chunk:archive.Chunks()) if(chunk.type=="PROC") for(const auto &entry:chunk.entries) {
                if(!name.empty()) throw std::runtime_error("Ambiguous map resource"); name=entry.name;
            }
            if(name.empty()) throw std::runtime_error("Map PROC absent");
            map=wolf2009::BuildStaticGameplayMap(wolf2009::ReadMapStaticCollision(input,archive,name),
                    wolf2009::ReadMapEntities(input,archive,name),argc==4?argv[3]:"");
        }
        const auto files=wolf2009::EncodeGameplayMap(map);
        Write(std::string(argv[2])+".map",files.mapText);
        Write(std::string(argv[2])+".script",files.scriptText);
        std::cout << "Generated " << files.worldBoxes << " exact world boxes, " << files.entityBoxes
                  << " entity boxes, " << files.entities << " native entities; "
                  << map.unimplementedEntities << " ENTS objects unimplemented\n";
        return 0;
    } catch(const std::exception &error) { std::cerr << "Gameplay map refused: " << error.what() << '\n'; return 1; }
}
