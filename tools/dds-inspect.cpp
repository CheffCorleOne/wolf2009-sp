// SPDX-License-Identifier: GPL-3.0-or-later
#include "Dds.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string(argv[1])=="--self-test")return wolf2009::RunDdsTests();
        if(argc<2||argc>3||std::string(argv[1])=="--help"){
            std::cout<<"dds-inspect INPUT.dds [NEW_OUTPUT.rgba]\ndds-inspect --self-test\nLegacy2D mip0 BC1/2/3 or tight A8 -> RGBA8, top-down. No color-space/normal-map conversion. Existing output is refused.\n";return argc<2?2:0;
        }
        std::ifstream file(argv[1],std::ios::binary);if(!file)throw std::runtime_error("Cannot open DDS input");
        file.seekg(0,std::ios::end);const std::streamoff size=file.tellg();const wolf2009::DdsLimits limits;
        if(size<0||static_cast<unsigned long long>(size)>limits.sourceBytes)throw std::runtime_error("DDS source exceeds limit or is not seekable");
        file.seekg(0,std::ios::beg);std::vector<unsigned char> source(static_cast<std::size_t>(size));
        if(!source.empty())file.read(reinterpret_cast<char*>(source.data()),std::streamsize(source.size()));if(!file)throw std::runtime_error("DDS read failed");
        const wolf2009::DdsImage image=wolf2009::DecodeDdsMip0(source);
        if(argc==3){std::FILE* output=std::fopen(argv[2],"wbx");if(!output)throw std::runtime_error("Cannot exclusively create RGBA output; existing paths are refused");
            const bool ok=std::fwrite(image.rgba.data(),1,image.rgba.size(),output)==image.rgba.size();const int closed=std::fclose(output);if(!ok||closed)throw std::runtime_error("RGBA write failed; incomplete output retained");}
        std::cout<<"width="<<image.width<<" height="<<image.height<<" format="<<image.format<<" mipCount="<<image.mipCount<<" rgbaBytes="<<image.rgba.size()<<" sourceFormatMarker=0x"<<std::hex<<image.sourceFormatMarker<<std::dec<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"dds-inspect: "<<e.what()<<'\n';return 1;}
}
