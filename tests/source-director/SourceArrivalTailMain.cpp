// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainArrival.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace {
std::vector<unsigned char> Read(const char* path){std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Owner-supplied arrival input absent");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
}
int main(int argc,char** argv){try{
    const int synthetic=wolf2009::RunTrainArrivalTests();if(synthetic)return synthetic;
    if(argc==4&&std::string(argv[1])=="--source"){
        const auto document=wolf2009::DecodeEntities4(Read(argv[2]));const wolf2009::LevelEntityRegistry registry(document);
        const auto bytes=Read(argv[3]);const auto preserved=bytes;const auto plan=wolf2009::DecodeTrainArrivalH1023(bytes,"brains/trainyard/movetrain",registry);
        const auto tail=wolf2009::QualifyTrainArrivalKeyedUseTail(plan);
        if(tail.sourceNodeId!=6||tail.targetName!="train_greeter_brainuse_1"||tail.targetClassname!="target_brainuse"||tail.useKey!="train_Stop"||
           tail.sourceHeader!=plan.unsupportedHeader||bytes!=preserved)throw std::runtime_error("Actual source arrival keyed-Use facts changed");
        std::cout<<"SOURCE_ARRIVAL_KEYED_TAIL_ORIGINAL_OK node="<<tail.sourceNodeId<<" raw_key="<<tail.useKey<<" metadata="<<tail.sourceHeader[2]<<','<<tail.sourceHeader[3]<<','<<tail.sourceHeader[4]<<" raw_source_unchanged=1 later_outputs=0 actor_ack=0\n";
    }else if(argc!=1)throw std::runtime_error("Usage: [--source owner-decoded-ENTS owner-BRAI]");
    return 0;
}catch(const std::exception& error){std::cerr<<"SOURCE_ARRIVAL_KEYED_TAIL_FAIL reason="<<error.what()<<'\n';return 1;}}
