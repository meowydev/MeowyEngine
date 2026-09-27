#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstring>
using namespace meowyrender;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    Check(SaveFileText("asset-check.obj","v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1 # triangle\n"),"write valid OBJ");
    auto model=LoadModel("asset-check.obj");Check(model.meshCount==1&&model.meshes[0].triangleCount==1,"OBJ relative indices and comments");UnloadModel(model);
    Check(SaveFileText("asset-invalid.obj","v 0 0 0\nf 1 2 3\n"),"write invalid OBJ");
    bool rejected=false;try{model=LoadModel("asset-invalid.obj");UnloadModel(model);}catch(const std::invalid_argument&){rejected=true;}Check(rejected,"OBJ bounds validation");
    const char truncated[]={static_cast<char>(0xf0),0};int size=0;Check(GetCodepointNext(truncated,&size)=='?'&&size==1,"truncated UTF8");
    Check(GetCodepointNext("\xed\xa0\x80",&size)=='?'&&size==1,"UTF8 surrogate rejected");
    Check(GetCodepointNext("\xf0\x9f\x90\xb1",&size)==0x1f431&&size==4,"UTF8 cat decoded");
    Check(GetCodepointCount("meow \xf0\x9f\x90\xb1")==6,"UTF8 count");
    Check(std::strcmp(CodepointToUTF8(-1,&size),"?")==0,"invalid codepoint rejected");
    std::puts("PASS: asset bounds and UTF8 safety");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
