//---------------------------------------------------------------------------
// ExportFBXNative for Metasequoia v2.2
// Scene-aware FBX 7.4 binary exporter.
// Implements mesh/object hierarchy, local transforms, per-face materials,
// per-corner smooth normals, all available UV channels, and edge indices.
// Smoothing-group masks are written as ByPolygon 32-bit bitmasks (3ds Max reads
// them directly as smoothing groups); crease-only meshes fall back to ByEdge 0/1.
// (creases force sharp; SG masks form the base layer).
// Material textures (diffuse/alpha/bump) are exported as external file
// references (Video + Texture nodes) with UV set binding.
//---------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <stdarg.h>
#include "MQBasePlugin.h"
#include "MQ3DLib.h"

static const uint64_t FBX_MAX_OFFSET = 0xFFFFFFFFull;
static const double MQ_TO_FBX_UNIT_SCALE = 1.0; // Scene coordinates are meters, matching ImportFBX.
static const DWORD SG_PRODUCT = 0x53473031;
static const DWORD SG_PLUGIN = 0x7C49A2E1;
static const char *SG_UDATA_NAME = "sgmask";
static const char *SG_EDGE_STATE_NAME = "edge_state";

class FbxWriter {
public:
    FbxWriter() : m_fp(NULL), m_ok(true) {}
    bool Open(const wchar_t *path) {
        m_buf.clear(); m_nodes.clear(); m_ok = true;
        m_fp = _wfopen(path, L"wb");
        if (!m_fp) return false;
        static const unsigned char magic[23] = {'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a','r','y',' ',' ','\0',0x1A,0x00};
        m_buf.insert(m_buf.end(), magic, magic + sizeof(magic)); WU32(7400);
        return true;
    }
    bool Close() {
        if (!m_fp) return false;
        bool ok = m_ok && m_nodes.empty();
        static const unsigned char f1[16] = {0xfa,0xbc,0xab,0x09,0xd0,0xc8,0xd4,0x66,0xb1,0x76,0xfb,0x83,0x1c,0xf7,0x26,0x7e};
        static const unsigned char z4[4] = {0,0,0,0};
        static const unsigned char ver[4] = {0xE8,0x1C,0x00,0x00};
        static const unsigned char z[120] = {0};
        static const unsigned char f2[16] = {0xf8,0x5a,0x8c,0x6a,0xde,0xf5,0xd9,0x7e,0xec,0xe9,0x0c,0xe3,0x75,0x8f,0x29,0x0b};
        __int64 pos = _ftelli64(m_fp);
        if (pos < 0) ok = false;
        if (ok) {
            unsigned pad = (unsigned)((16 - (((uint64_t)pos + 20ull) & 15ull)) & 15ull);
            static const unsigned char zeros[16] = {0};
            ok = fwrite(zeros, 1, pad, m_fp) == pad;
        }
        ok = ok && fwrite(f1,1,sizeof(f1),m_fp)==sizeof(f1);
        ok = ok && fwrite(z4,1,sizeof(z4),m_fp)==sizeof(z4);
        ok = ok && fwrite(ver,1,sizeof(ver),m_fp)==sizeof(ver);
        ok = ok && fwrite(z,1,sizeof(z),m_fp)==sizeof(z);
        ok = ok && fwrite(f2,1,sizeof(f2),m_fp)==sizeof(f2);
        ok = (fclose(m_fp)==0) && ok; m_fp=NULL;
        return ok;
    }
    void Begin(const char *name) {
        if (!m_ok || !name) { m_ok=false; return; }
        size_t n = strlen(name);
        if (!n || n > 255 || m_buf.size() > FBX_MAX_OFFSET) { m_ok=false; return; }
        if (!m_nodes.empty() && m_nodes.back().childStart == 0)
            m_nodes.back().childStart = (uint32_t)m_buf.size();
        m_nodes.push_back(NodeState());
        NodeState &s=m_nodes.back(); s.start=(uint32_t)m_buf.size(); s.propStart=0; s.childStart=0; s.props=0;
        WU32(0); WU32(0); WU32(0); WU8((uint8_t)n);
        m_buf.insert(m_buf.end(), name, name+n); s.propStart=(uint32_t)m_buf.size();
    }
    void End(bool = false) {
        if (!m_ok || m_nodes.empty()) { m_ok=false; return; }
        NodeState s=m_nodes.back();
        uint64_t current=m_buf.size();
        uint64_t propEnd=s.childStart ? s.childStart : current;
        if (propEnd < s.propStart || current > FBX_MAX_OFFSET-13ull) { m_ok=false; return; }
        uint64_t propLen=propEnd-s.propStart;
        if (s.childStart) for(int i=0;i<13;i++) m_buf.push_back(0);
        uint64_t end=m_buf.size();
        if (end > FBX_MAX_OFFSET || s.start+13ull > m_buf.size() || propLen > FBX_MAX_OFFSET) { m_ok=false; return; }
        PatchU32(s.start,(uint32_t)end); PatchU32(s.start+4,s.props); PatchU32(s.start+8,(uint32_t)propLen);
        m_nodes.pop_back();
    }
    void PropI32(int32_t v) { PHead('I'); WU32((uint32_t)v); }
    void PropBool(bool v) { PHead('C'); WU8(v?1:0); }
    void PropI64(int64_t v) { PHead('L'); WU64((uint64_t)v); }
    void PropF64(double v) { PHead('D'); WU64(DoubleBits(v)); }
    void PropStr(const std::string &s) {
        if (s.size()>0xFFFFFFFFull) { m_ok=false; return; }
        PHead('S'); WU32((uint32_t)s.size()); m_buf.insert(m_buf.end(),s.begin(),s.end());
    }
    void PropRaw(const std::vector<unsigned char> &v) {
        if (v.size()>0xFFFFFFFFull) { m_ok=false; return; }
        PHead('R'); WU32((uint32_t)v.size()); m_buf.insert(m_buf.end(),v.begin(),v.end());
    }
    void PropI32Array(const std::vector<int32_t> &v) {
        if (v.size()>0xFFFFFFFFull || v.size()>0xFFFFFFFFull/4ull) { m_ok=false; return; }
        PHead('i'); WU32((uint32_t)v.size()); WU32(0); WU32((uint32_t)(v.size()*4ull));
        for (size_t i=0;i<v.size();++i) WU32((uint32_t)v[i]);
    }
    void PropF64Array(const std::vector<double> &v) {
        if (v.size()>0xFFFFFFFFull || v.size()>0xFFFFFFFFull/8ull) { m_ok=false; return; }
        PHead('d'); WU32((uint32_t)v.size()); WU32(0); WU32((uint32_t)(v.size()*8ull));
        for (size_t i=0;i<v.size();++i) WU64(DoubleBits(v[i]));
    }
    void Abort() { if(m_fp){fclose(m_fp);m_fp=NULL;} m_buf.clear();m_nodes.clear();m_ok=false; }
    bool Flush() {
        if (!m_fp || !m_ok || !m_nodes.empty()) return false;
        for(int i=0;i<13;i++) WU8(0);
        if (m_buf.size()>FBX_MAX_OFFSET) return false;
        size_t n=fwrite(m_buf.data(),1,m_buf.size(),m_fp);
        if(n!=m_buf.size()) { m_ok=false; return false; }
        m_buf.clear(); return true;
    }
    bool Good() const { return m_ok; }
private:
    struct NodeState { uint32_t start,propStart,props,childStart; NodeState():start(0),propStart(0),props(0),childStart(0){} };
    FILE *m_fp; bool m_ok; std::vector<unsigned char> m_buf; std::vector<NodeState> m_nodes;
    void PHead(unsigned char t) { if(m_nodes.empty()){m_ok=false;return;} WU8(t); ++m_nodes.back().props; }
    void WU8(uint8_t v){m_buf.push_back(v);}
    void WU32(uint32_t v){m_buf.push_back((uint8_t)v);m_buf.push_back((uint8_t)(v>>8));m_buf.push_back((uint8_t)(v>>16));m_buf.push_back((uint8_t)(v>>24));}
    void WU64(uint64_t v){for(int i=0;i<8;i++)m_buf.push_back((uint8_t)(v>>(i*8)));}
    static uint64_t DoubleBits(double d){uint64_t u;memcpy(&u,&d,8);return u;}
    void PatchU32(size_t p,uint32_t v){m_buf[p]=(uint8_t)v;m_buf[p+1]=(uint8_t)(v>>8);m_buf[p+2]=(uint8_t)(v>>16);m_buf[p+3]=(uint8_t)(v>>24);}
};

static std::string ObjName(const std::string &name,const char *cls){std::string s=name;s.push_back('\0');s.push_back('\1');s+=cls;return s;}
static std::string SafeName(const std::wstring &w,const char *fallback){std::string s=MQEncoding::WideToUtf8(w.c_str());return s.empty()?std::string(fallback):s;}
static bool Finite(double v){return (v==v && v<=std::numeric_limits<double>::max() && v>=-std::numeric_limits<double>::max());}
static double Clamp01(double x){return x<0?0:(x>1?1:x);}

static void PBegin(FbxWriter &w,const char *name,const char *type,const char *label,const char *flags){
    w.Begin("P");w.PropStr(name);w.PropStr(type);w.PropStr(label);w.PropStr(flags);
}
static void PEnd(FbxWriter &w){w.End();}
static double SafeNumber(double v){return Finite(v)?v:0.0;}
static void PScalar(FbxWriter &w,const char *name,const char *type,const char *label,const char *flags,double v){PBegin(w,name,type,label,flags);w.PropF64(SafeNumber(v));PEnd(w);}
static void PInt(FbxWriter &w,const char *name,int v){PBegin(w,name,"int","Integer","");w.PropI32(v);PEnd(w);}
static void PEnum(FbxWriter &w,const char *name,int v){PBegin(w,name,"enum","","");w.PropI32(v);PEnd(w);}
static void PString(FbxWriter &w,const char *name,const char *v){PBegin(w,name,"KString","","");w.PropStr(v);PEnd(w);}
static void PBool(FbxWriter &w,const char *name,bool v){PBegin(w,name,"bool","","");w.PropI32(v?1:0);PEnd(w);}
static void PVectorMeta(FbxWriter &w,const char *name,const char *type,const char *label,const char *flags,double x,double y,double z){PBegin(w,name,type,label,flags);w.PropF64(SafeNumber(x));w.PropF64(SafeNumber(y));w.PropF64(SafeNumber(z));PEnd(w);}
static void PColorRGB(FbxWriter &w,const char *name,double r,double g,double b){PVectorMeta(w,name,"ColorRGB","Color","",r,g,b);}
static void PColor(FbxWriter &w,const char *name,double r,double g,double b){PVectorMeta(w,name,"Color","","A",r,g,b);}

struct TextureSlotData {
    std::wstring absolute, relative, uvSet;
    int64_t textureId, videoId;
    std::string videoNodeName;
    TextureSlotData():textureId(0),videoId(0){}
    bool Valid() const { return !absolute.empty(); }
};

// These template definitions mirror the corresponding templates in the
// known-good Metasequoia-generated UCtank.fbx (FbxNode: 71, FbxMesh: 6,
// FbxSurfacePhong: 24 properties), including FBX property storage types.
static void WriteNodeTemplate(FbxWriter &w){
    w.Begin("PropertyTemplate");w.PropStr("FbxNode");w.Begin("Properties70");
    PEnum(w,"QuaternionInterpolate",0);
    PVectorMeta(w,"RotationOffset","Vector3D","Vector","",0,0,0);
    PVectorMeta(w,"RotationPivot","Vector3D","Vector","",0,0,0);
    PVectorMeta(w,"ScalingOffset","Vector3D","Vector","",0,0,0);
    PVectorMeta(w,"ScalingPivot","Vector3D","Vector","",0,0,0);
    PBool(w,"TranslationActive",false);
    PVectorMeta(w,"TranslationMin","Vector3D","Vector","",0,0,0);
    PVectorMeta(w,"TranslationMax","Vector3D","Vector","",0,0,0);
    PBool(w,"TranslationMinX",false);PBool(w,"TranslationMinY",false);PBool(w,"TranslationMinZ",false);
    PBool(w,"TranslationMaxX",false);PBool(w,"TranslationMaxY",false);PBool(w,"TranslationMaxZ",false);
    PEnum(w,"RotationOrder",0);PBool(w,"RotationSpaceForLimitOnly",false);
    PScalar(w,"RotationStiffnessX","double","Number","",0);PScalar(w,"RotationStiffnessY","double","Number","",0);PScalar(w,"RotationStiffnessZ","double","Number","",0);
    PScalar(w,"AxisLen","double","Number","",10);
    PVectorMeta(w,"PreRotation","Vector3D","Vector","",0,0,0);PVectorMeta(w,"PostRotation","Vector3D","Vector","",0,0,0);
    PBool(w,"RotationActive",false);PVectorMeta(w,"RotationMin","Vector3D","Vector","",0,0,0);PVectorMeta(w,"RotationMax","Vector3D","Vector","",0,0,0);
    PBool(w,"RotationMinX",false);PBool(w,"RotationMinY",false);PBool(w,"RotationMinZ",false);PBool(w,"RotationMaxX",false);PBool(w,"RotationMaxY",false);PBool(w,"RotationMaxZ",false);
    PEnum(w,"InheritType",0);PBool(w,"ScalingActive",false);
    PVectorMeta(w,"ScalingMin","Vector3D","Vector","",0,0,0);PVectorMeta(w,"ScalingMax","Vector3D","Vector","",1,1,1);
    PBool(w,"ScalingMinX",false);PBool(w,"ScalingMinY",false);PBool(w,"ScalingMinZ",false);PBool(w,"ScalingMaxX",false);PBool(w,"ScalingMaxY",false);PBool(w,"ScalingMaxZ",false);
    PVectorMeta(w,"GeometricTranslation","Vector3D","Vector","",0,0,0);PVectorMeta(w,"GeometricRotation","Vector3D","Vector","",0,0,0);PVectorMeta(w,"GeometricScaling","Vector3D","Vector","",1,1,1);
    PScalar(w,"MinDampRangeX","double","Number","",0);PScalar(w,"MinDampRangeY","double","Number","",0);PScalar(w,"MinDampRangeZ","double","Number","",0);
    PScalar(w,"MaxDampRangeX","double","Number","",0);PScalar(w,"MaxDampRangeY","double","Number","",0);PScalar(w,"MaxDampRangeZ","double","Number","",0);
    PScalar(w,"MinDampStrengthX","double","Number","",0);PScalar(w,"MinDampStrengthY","double","Number","",0);PScalar(w,"MinDampStrengthZ","double","Number","",0);
    PScalar(w,"MaxDampStrengthX","double","Number","",0);PScalar(w,"MaxDampStrengthY","double","Number","",0);PScalar(w,"MaxDampStrengthZ","double","Number","",0);
    PScalar(w,"PreferedAngleX","double","Number","",0);PScalar(w,"PreferedAngleY","double","Number","",0);PScalar(w,"PreferedAngleZ","double","Number","",0);
    PBegin(w,"LookAtProperty","object","","");PEnd(w);PBegin(w,"UpVectorProperty","object","","");PEnd(w);
    PBool(w,"Show",true);PBool(w,"NegativePercentShapeSupport",true);PInt(w,"DefaultAttributeIndex",-1);PBool(w,"Freeze",false);PBool(w,"LODBox",false);
    PVectorMeta(w,"Lcl Translation","Lcl Translation","","A",0,0,0);PVectorMeta(w,"Lcl Rotation","Lcl Rotation","","A",0,0,0);PVectorMeta(w,"Lcl Scaling","Lcl Scaling","","A",1,1,1);
    PScalar(w,"Visibility","Visibility","","A",1);PBegin(w,"Visibility Inheritance","Visibility Inheritance","","");w.PropI32(1);PEnd(w);
    w.End(true);w.End(true);
}
static void WriteNullTemplate(FbxWriter &w){
    w.Begin("PropertyTemplate");w.PropStr("FbxNull");w.Begin("Properties70");
    PColorRGB(w,"Color",0.8,0.8,0.8);PScalar(w,"Size","double","Number","",100);PEnum(w,"Look",1);
    w.End(true);w.End(true);
}
static void WriteGeometryTemplate(FbxWriter &w){
    w.Begin("PropertyTemplate");w.PropStr("FbxMesh");w.Begin("Properties70");
    PColorRGB(w,"Color",0.8,0.8,0.8);PVectorMeta(w,"BBoxMin","Vector3D","Vector","",0,0,0);PVectorMeta(w,"BBoxMax","Vector3D","Vector","",0,0,0);
    PBool(w,"Primary Visibility",true);PBool(w,"Casts Shadows",true);PBool(w,"Receive Shadows",true);
    w.End(true);w.End(true);
}
static void WriteMaterialTemplate(FbxWriter &w){
    w.Begin("PropertyTemplate");w.PropStr("FbxSurfacePhong");w.Begin("Properties70");
    PString(w,"ShadingModel","Phong");PBool(w,"MultiLayer",false);
    PColor(w,"EmissiveColor",0,0,0);PScalar(w,"EmissiveFactor","Number","","A",1);
    PColor(w,"AmbientColor",0.2,0.2,0.2);PScalar(w,"AmbientFactor","Number","","A",1);
    PColor(w,"DiffuseColor",0.8,0.8,0.8);PScalar(w,"DiffuseFactor","Number","","A",1);
    PColor(w,"TransparentColor",0,0,0);PScalar(w,"TransparencyFactor","Number","","A",0);PScalar(w,"Opacity","Number","","A",1);
    PVectorMeta(w,"NormalMap","Vector3D","Vector","",0,0,0);PVectorMeta(w,"Bump","Vector3D","Vector","",0,0,0);
    PScalar(w,"BumpFactor","double","Number","",1);PColorRGB(w,"DisplacementColor",0,0,0);PScalar(w,"DisplacementFactor","double","Number","",1);
    PColorRGB(w,"VectorDisplacementColor",0,0,0);PScalar(w,"VectorDisplacementFactor","double","Number","",1);
    PColor(w,"SpecularColor",0.2,0.2,0.2);PScalar(w,"SpecularFactor","Number","","A",1);
    PScalar(w,"Shininess","Number","","A",20);PScalar(w,"ShininessExponent","Number","","A",20);
    PColor(w,"ReflectionColor",0,0,0);PScalar(w,"ReflectionFactor","Number","","A",1);
    w.End(true);w.End(true);
}
static void WriteTextureTemplate(FbxWriter &w){
    w.Begin("PropertyTemplate");w.PropStr("FbxFileTexture");w.Begin("Properties70");
    PVectorMeta(w,"Translation","Vector3D","Vector","",0,0,0);PVectorMeta(w,"Rotation","Vector3D","Vector","",0,0,0);PVectorMeta(w,"Scaling","Vector3D","Vector","",1,1,1);
    PEnum(w,"TextureTypeUse",0);PEnum(w,"WrapModeU",0);PEnum(w,"WrapModeV",0);PString(w,"UVSet","");PBool(w,"UseMaterial",false);
    w.End(true);w.End(true);
}
static void WriteVideoTemplate(FbxWriter &w){
    w.Begin("PropertyTemplate");w.PropStr("FbxVideo");w.Begin("Properties70");
    PBegin(w,"Path","KString","XRefUrl","");w.PropStr("");PEnd(w);
    w.End(true);w.End(true);
}
static void WriteTexture(FbxWriter &w,const TextureSlotData &t,const std::string &name){
    w.Begin("Texture");w.PropI64(t.textureId);w.PropStr(ObjName(name,"Texture"));w.PropStr("");
    w.Begin("Type");w.PropStr("TextureVideoClip");w.End();
    w.Begin("Version");w.PropI32(202);w.End();
    w.Begin("TextureName");w.PropStr(name);w.End();
    w.Begin("Properties70");
    PVectorMeta(w,"Translation","Vector3D","Vector","",0,0,0);PVectorMeta(w,"Rotation","Vector3D","Vector","",0,0,0);PVectorMeta(w,"Scaling","Vector3D","Vector","",1,1,1);
    PEnum(w,"TextureTypeUse",0);PEnum(w,"WrapModeU",0);PEnum(w,"WrapModeV",0);
    PString(w,"UVSet",MQEncoding::WideToUtf8(t.uvSet.c_str()).c_str());
    PBool(w,"UseMaterial",true);
    w.End(true);
    w.Begin("Media");w.PropStr("Video::"+t.videoNodeName);w.End();
    w.End(true);
}
static void WriteVideo(FbxWriter &w,int64_t id,const std::string &nodeName,const std::string &absPath,const std::string &relPath){
    w.Begin("Video");w.PropI64(id);w.PropStr(ObjName(nodeName,"Video"));w.PropStr("Clip");
    w.Begin("Type");w.PropStr("Clip");w.End();
    w.Begin("Properties70");PBegin(w,"Path","KString","XRefUrl","");w.PropStr(absPath);PEnd(w);w.End(true);
    w.Begin("UseMipMap");w.PropI32(0);w.End();
    w.Begin("Filename");w.PropStr(absPath);w.End();
    w.Begin("RelativeFilename");w.PropStr(relPath);w.End();
    w.End(true);
}

struct UVSetData { UINT id; std::string name; bool valid; std::vector<double> uv; std::vector<int32_t> indices; UVSetData():id(0),valid(true){} };
struct MeshData {
    MQObject obj; int sourceIndex; bool hasMesh; std::string name;
    int64_t modelId,geomId,attributeId; MQPoint scale,trans; MQAngle rot;
    std::vector<double> vertices,normals; std::vector<int32_t> polygonIndices,edges,faceMaterials,smoothing,faceMasks;
    std::vector<UVSetData> uvSets; std::vector<int> usedMaterials;
    int smoothingMode;
    double bboxMin[3],bboxMax[3];
    MeshData():obj(NULL),sourceIndex(-1),hasMesh(false),modelId(0),geomId(0),attributeId(0),smoothingMode(0){bboxMin[0]=bboxMin[1]=bboxMin[2]=0;bboxMax[0]=bboxMax[1]=bboxMax[2]=0;}
};
struct MaterialData { int sourceIndex; int64_t id; std::string name; MQMaterial material; TextureSlotData diffuse,alpha,bump; MaterialData():sourceIndex(-1),id(0),material(NULL){} };

// Resolve a Metasequoia material texture path; fall back to the stored path.
static void FillTextureSlot(MQDocument doc,const wchar_t *fbxPath,const std::wstring &stored,UINT uvChannel,TextureSlotData &t){
    if(stored.empty())return;
    wchar_t resolved[MAX_PATH]={0};
    if(doc->FindMappingFile(resolved,stored.c_str(),MQMAPPING_TEXTURE)&&resolved[0])t.absolute=resolved;
    else t.absolute=stored; // keep stored (possibly relative) so downstream can still try
    // Relative path from the FBX directory to the texture file.
    std::wstring fdir(fbxPath?fbxPath:L"");
    size_t slash=fdir.find_last_of(L"\\/");
    fdir=(slash==std::wstring::npos)?L".":fdir.substr(0,slash);
    wchar_t rel[MAX_PATH*2]={0};
    if(PathRelativePathToW(rel,fdir.c_str(),FILE_ATTRIBUTE_DIRECTORY,t.absolute.c_str(),0)&&rel[0])t.relative=rel;
    else t.relative=t.absolute;
    std::wstring uvName=doc->GetUVChannelName(uvChannel);
    t.uvSet=uvName.empty()?(L"UVSet"+std::to_wstring((unsigned long long)uvChannel)):uvName;
}

class FbxExportLog {
    FILE *fp;
public:
    explicit FbxExportLog(const wchar_t *fbxPath):fp(NULL){std::wstring p(fbxPath?fbxPath:L"");p+=L".log";fp=_wfopen(p.c_str(),L"wt");}
    ~FbxExportLog(){if(fp)fclose(fp);}
    void Line(const wchar_t *fmt,...){if(!fp)return;va_list ap;va_start(ap,fmt);vfwprintf(fp,fmt,ap);va_end(ap);fputwc(L'\n',fp);fflush(fp);}
};

struct PatchGridKey {
    int x,y,z;
    bool operator<(const PatchGridKey &o) const { return x!=o.x?x<o.x:(y!=o.y?y<o.y:z<o.z); }
};
struct PatchSourceEdge { int a,b; float crease,length; };

static PatchGridKey PatchCell(const MQPoint &p,float size){
    PatchGridKey k={ (int)floor(p.x/size),(int)floor(p.y/size),(int)floor(p.z/size) };return k;
}
static double PatchDistance2(const MQPoint &a,const MQPoint &b){
    double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;
}
static double PatchSegmentDistance2(const MQPoint &p,const MQPoint &a,const MQPoint &b){
    double ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
    double len=ux*ux+uy*uy+uz*uz;if(len<1e-20)return PatchDistance2(p,a);
    double t=((p.x-a.x)*ux+(p.y-a.y)*uy+(p.z-a.z)*uz)/len;
    t=std::max(0.0,std::min(1.0,t));
    MQPoint q(a.x+(float)(ux*t),a.y+(float)(uy*t),a.z+(float)(uz*t));
    return PatchDistance2(p,q);
}

static void PropagatePatchData(MQObject source,MQObject frozen,MQDocument doc){
    int faceUid=doc->FindFaceUserData(SG_PRODUCT,SG_PLUGIN,SG_UDATA_NAME);
    int edgeUid=doc->FindObjectUserData(SG_PRODUCT,SG_PLUGIN,SG_EDGE_STATE_NAME);
    int nf=source->GetFaceCount();if(nf<=0)return;
    std::map<PatchGridKey,std::vector<int> > faceGrid;
    std::vector<MQPoint> centroids(nf);
    std::vector<unsigned int> masks(nf,0);
    std::map<std::pair<int,int>,PatchSourceEdge> sourceEdges;
    double totalLength=0;
    for(int fi=0;fi<nf;fi++){
        int n=source->GetFacePointCount(fi);if(n<3)continue;
        std::vector<int> vi((size_t)n);source->GetFacePointArray(fi,vi.data());
        MQPoint c(0,0,0);
        for(int i=0;i<n;i++){MQPoint p=source->GetVertex(vi[i]);c.x+=p.x/n;c.y+=p.y/n;c.z+=p.z/n;}
        centroids[fi]=c;
        if(faceUid>0)source->GetFaceUserData(faceUid,fi,&masks[fi]);
        for(int i=0;i<n;i++){
            int a=vi[i],b=vi[(i+1)%n];std::pair<int,int> key=std::make_pair(std::min(a,b),std::max(a,b));
            float crease=source->GetFaceEdgeCrease(fi,i);
            double len=sqrt(PatchDistance2(source->GetVertex(a),source->GetVertex(b)));
            auto found=sourceEdges.find(key);
            if(found==sourceEdges.end())sourceEdges[key]=PatchSourceEdge{a,b,crease,(float)len};
            else found->second.crease=std::max(found->second.crease,crease);
            totalLength+=len;
        }
    }
    float cellSize=sourceEdges.empty()?1.0f:std::max((float)(totalLength/sourceEdges.size()),1e-3f);
    for(int fi=0;fi<nf;fi++)if(source->GetFacePointCount(fi)>=3)faceGrid[PatchCell(centroids[fi],cellSize)].push_back(fi);
    std::map<PatchGridKey,std::vector<PatchSourceEdge> > edgeGrid;
    for(const auto &kv:sourceEdges){
        const PatchSourceEdge &e=kv.second;
        MQPoint mid((source->GetVertex(e.a).x+source->GetVertex(e.b).x)*0.5f,
                    (source->GetVertex(e.a).y+source->GetVertex(e.b).y)*0.5f,
                    (source->GetVertex(e.a).z+source->GetVertex(e.b).z)*0.5f);
        edgeGrid[PatchCell(mid,cellSize)].push_back(e);
    }
    if(edgeUid>0){char enabled=0;if(source->GetUserData(edgeUid,&enabled)&&enabled){frozen->AllocUserData(edgeUid);frozen->SetUserData(edgeUid,&enabled);}}
    if(faceUid>0)frozen->AllocFaceUserData(faceUid);

    int frozenFaces=frozen->GetFaceCount();
    for(int fi=0;fi<frozenFaces;fi++){
        int n=frozen->GetFacePointCount(fi);if(n<3)continue;
        std::vector<int> vi((size_t)n);frozen->GetFacePointArray(fi,vi.data());
        MQPoint c(0,0,0);
        for(int i=0;i<n;i++){MQPoint p=frozen->GetVertex(vi[i]);c.x+=p.x/n;c.y+=p.y/n;c.z+=p.z/n;}
        PatchGridKey center=PatchCell(c,cellSize);int bestFace=-1;double bestDistance=1e300;
        for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++)for(int dz=-1;dz<=1;dz++){
            PatchGridKey key={center.x+dx,center.y+dy,center.z+dz};
            auto it=faceGrid.find(key);if(it==faceGrid.end())continue;
            for(int candidate:it->second){double d=PatchDistance2(c,centroids[candidate]);if(d<bestDistance){bestDistance=d;bestFace=candidate;}}
        }
        if(faceUid>0&&bestFace>=0)frozen->SetFaceUserData(faceUid,fi,&masks[bestFace]);
        for(int i=0;i<n;i++){
            MQPoint a=frozen->GetVertex(vi[i]),b=frozen->GetVertex(vi[(i+1)%n]);
            MQPoint mid((a.x+b.x)*0.5f,(a.y+b.y)*0.5f,(a.z+b.z)*0.5f);
            PatchGridKey edgeCenter=PatchCell(mid,cellSize);float bestCrease=0,bestLength=0;double bestScore=1e300;
            for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++)for(int dz=-1;dz<=1;dz++){
                PatchGridKey key={edgeCenter.x+dx,edgeCenter.y+dy,edgeCenter.z+dz};
                auto it=edgeGrid.find(key);if(it==edgeGrid.end())continue;
                for(const PatchSourceEdge &e:it->second){
                    double score=std::max(PatchSegmentDistance2(a,source->GetVertex(e.a),source->GetVertex(e.b)),
                                          PatchSegmentDistance2(b,source->GetVertex(e.a),source->GetVertex(e.b)));
                    if(score<bestScore){bestScore=score;bestCrease=e.crease;bestLength=e.length;}
                }
            }
            double tolerance=std::max(bestLength*0.05,1e-4);
            if(bestCrease>0&&bestScore<=tolerance*tolerance)frozen->SetFaceEdgeCrease(fi,i,bestCrease);
        }
    }
}

static bool BuildMeshData(MQDocument doc,MQObject sceneObj,MQObject geometryObj,int sourceIndex,MeshData &m){
    MQObject obj=geometryObj;
    m.obj=sceneObj;m.sourceIndex=sourceIndex;m.name=SafeName(sceneObj->GetNameW(),("Object"+std::to_string(sourceIndex)).c_str());
    m.scale=sceneObj->GetScaling();m.rot=sceneObj->GetRotation();m.trans=sceneObj->GetTranslation()*(float)MQ_TO_FBX_UNIT_SCALE;
    if(!Finite(m.scale.x)||!Finite(m.scale.y)||!Finite(m.scale.z)||!Finite(m.trans.x)||!Finite(m.trans.y)||!Finite(m.trans.z)||!Finite(m.rot.head)||!Finite(m.rot.pich)||!Finite(m.rot.bank))return false;
    int nv=obj->GetVertexCount(),nf=obj->GetFaceCount();
    if(nv<=0||nf<=0){m.hasMesh=false;return true;}
    m.vertices.reserve((size_t)nv*3);
    for(int i=0;i<nv;i++){
        MQPoint p=obj->GetVertex(i)*(float)MQ_TO_FBX_UNIT_SCALE;double x=p.x,y=p.y,z=p.z;
        if(!Finite(x)||!Finite(y)||!Finite(z))return false;
        m.vertices.push_back(x);m.vertices.push_back(y);m.vertices.push_back(z);
        if(i==0){m.bboxMin[0]=m.bboxMax[0]=x;m.bboxMin[1]=m.bboxMax[1]=y;m.bboxMin[2]=m.bboxMax[2]=z;}
        else {m.bboxMin[0]=std::min(m.bboxMin[0],x);m.bboxMax[0]=std::max(m.bboxMax[0],x);m.bboxMin[1]=std::min(m.bboxMin[1],y);m.bboxMax[1]=std::max(m.bboxMax[1],y);m.bboxMin[2]=std::min(m.bboxMin[2],z);m.bboxMax[2]=std::max(m.bboxMax[2],z);}
    }
    std::vector<UINT> channels;int channelCount=obj->GetUVChannelNum();
    for(int i=0;i<channelCount;i++){UINT id=0;if(obj->GetUVChannel(i,id)){if(std::find(channels.begin(),channels.end(),id)==channels.end())channels.push_back(id);}}
    for(size_t i=0;i<channels.size();i++){
        UVSetData u;u.id=channels[i];std::wstring wn=doc->GetUVChannelName(u.id);u.name=SafeName(wn,("UVSet"+std::to_string((unsigned long long)u.id)).c_str());m.uvSets.push_back(u);
    }

    int edgeUid=doc->FindObjectUserData(SG_PRODUCT,SG_PLUGIN,SG_EDGE_STATE_NAME);
    char edgeState=0;
    if(edgeUid>0&&sceneObj->GetUserData(edgeUid,&edgeState)&&edgeState)m.smoothingMode|=1;
    int smoothingUid=doc->FindFaceUserData(SG_PRODUCT,SG_PLUGIN,SG_UDATA_NAME);
    if(smoothingUid>0)m.smoothingMode|=2;
    struct EdgeIncident { std::vector<uint32_t> masks; std::vector<char> sharp; };
    std::map<std::pair<int,int>,int32_t> edgeMap;
    std::vector<EdgeIncident> edgeIncidents;

    MQObjNormal normals(obj);
    for(int fi=0;fi<nf;fi++){
        if(!obj->GetFaceVisible(fi))continue;
        int n=obj->GetFacePointCount(fi);if(n<3)continue;
        std::vector<int> vi((size_t)n);obj->GetFacePointArray(fi,vi.data());
        bool valid=true;for(int k=0;k<n;k++)if(vi[k]<0||vi[k]>=nv){valid=false;break;}if(!valid)return false;
        int mat=obj->GetFaceMaterial(fi);if(mat<0||mat>=doc->GetMaterialCount())mat=-1;
        if(std::find(m.usedMaterials.begin(),m.usedMaterials.end(),mat)==m.usedMaterials.end())m.usedMaterials.push_back(mat);
        uint32_t faceMask=0;if(m.smoothingMode&2)obj->GetFaceUserData(smoothingUid,fi,&faceMask);
        int32_t localCorner=(int32_t)m.polygonIndices.size();
        for(int k=0;k<n;k++){
            int originalCorner=n-1-k;int v=vi[originalCorner];
            m.polygonIndices.push_back(k==n-1?-(v+1):v);
            MQPoint q=normals.Get(fi,originalCorner);q.normalize();double nx=q.x,ny=q.y,nz=q.z;
            double len=sqrt(nx*nx+ny*ny+nz*nz);if(len>1e-20){nx/=len;ny/=len;nz/=len;}else{nx=ny=nz=0;}
            m.normals.push_back(nx);m.normals.push_back(ny);m.normals.push_back(nz);
            for(size_t ui=0;ui<m.uvSets.size();ui++){
                MQCoordinate uv(0,0);if(!obj->GetFaceChannelCoordinate(fi,originalCorner,m.uvSets[ui].id,uv)){m.uvSets[ui].valid=false;uv=MQCoordinate(0,0);}
                if(!Finite(uv.u)||!Finite(uv.v))return false;
                m.uvSets[ui].uv.push_back(uv.u);m.uvSets[ui].uv.push_back(uv.v);
                m.uvSets[ui].indices.push_back((int32_t)m.uvSets[ui].indices.size());
            }
            int a=v;
            int previousCorner=(originalCorner-1+n)%n;
            int b=vi[previousCorner];
            std::pair<int,int> key=std::make_pair(std::min(a,b),std::max(a,b));
            auto found=edgeMap.find(key);int32_t edgeIndex;
            if(found==edgeMap.end()){
                edgeIndex=(int32_t)m.edges.size();edgeMap[key]=edgeIndex;
                m.edges.push_back(localCorner+k);edgeIncidents.push_back(EdgeIncident());
            }else edgeIndex=found->second;
            if(m.smoothingMode&1)edgeIncidents[edgeIndex].sharp.push_back(obj->GetFaceEdgeCrease(fi,previousCorner)>=0.5f?1:0);
            if(m.smoothingMode&2)edgeIncidents[edgeIndex].masks.push_back(faceMask);
        }
        m.faceMaterials.push_back(mat);
        m.faceMasks.push_back((int32_t)faceMask);
    }
    if(m.polygonIndices.empty()){m.hasMesh=false;return true;}
    if(m.smoothingMode){
        m.smoothing.reserve(edgeIncidents.size());
        for(size_t i=0;i<edgeIncidents.size();i++){
            bool smooth=true;
            // Unified hard-edge rule: an explicit crease always forces sharp.
            if(m.smoothingMode&1){
                for(size_t j=0;j<edgeIncidents[i].sharp.size();j++)if(edgeIncidents[i].sharp[j]){smooth=false;break;}
            }
            if(smooth&&(m.smoothingMode&2)){
                if(m.smoothingMode&1){
                    // Crease+SG combined: only faces assigned to at least one group
                    // impose sharpness; unassigned faces defer to crease marks.
                    for(size_t a=0;a<edgeIncidents[i].masks.size()&&smooth;a++)
                        for(size_t b=a+1;b<edgeIncidents[i].masks.size();b++)
                            if(edgeIncidents[i].masks[a]!=0&&edgeIncidents[i].masks[b]!=0&&(edgeIncidents[i].masks[a]&edgeIncidents[i].masks[b])==0){smooth=false;break;}
                }else{
                    // SG-only: no shared group (including unassigned faces) means sharp.
                    for(size_t a=0;a<edgeIncidents[i].masks.size()&&smooth;a++)
                        for(size_t b=a+1;b<edgeIncidents[i].masks.size();b++)
                            if((edgeIncidents[i].masks[a]&edgeIncidents[i].masks[b])==0){smooth=false;break;}
                }
            }
            m.smoothing.push_back(smooth?1:0);
        }
    }
    m.uvSets.erase(std::remove_if(m.uvSets.begin(),m.uvSets.end(),[](const UVSetData &u){return !u.valid;}),m.uvSets.end());
    m.hasMesh=true;return true;
}

static void WriteModelProperties(FbxWriter &w,const MeshData &m){
    w.Begin("Properties70");
    PVectorMeta(w,"Lcl Translation","Lcl Translation","","A",m.trans.x,m.trans.y,m.trans.z);
    // Metasequoia stores heading(Y), pitch(X), bank(Z); FBX order 3 is YXZ.
    PVectorMeta(w,"Lcl Rotation","Lcl Rotation","","A",m.rot.pich,m.rot.head,m.rot.bank);
    PVectorMeta(w,"Lcl Scaling","Lcl Scaling","","A",m.scale.x,m.scale.y,m.scale.z);
    PEnum(w,"RotationOrder",3);PEnum(w,"InheritType",1);
    PScalar(w,"Visibility","Visibility","","A",1.0);PBegin(w,"Visibility Inheritance","Visibility Inheritance","","");w.PropI32(1);PEnd(w);
    PInt(w,"DefaultAttributeIndex",0);
    w.End(true);
}

static void WriteGeometry(FbxWriter &w,const MeshData &m){
    w.Begin("Geometry");w.PropI64(m.geomId);w.PropStr(ObjName(m.name,"Geometry"));w.PropStr("Mesh");
    w.Begin("Properties70");PColorRGB(w,"Color",0.8,0.8,0.8);PVectorMeta(w,"BBoxMin","Vector3D","Vector","",m.bboxMin[0],m.bboxMin[1],m.bboxMin[2]);PVectorMeta(w,"BBoxMax","Vector3D","Vector","",m.bboxMax[0],m.bboxMax[1],m.bboxMax[2]);w.End(true);
    w.Begin("GeometryVersion");w.PropI32(124);w.End();w.Begin("Vertices");w.PropF64Array(m.vertices);w.End();
    w.Begin("PolygonVertexIndex");w.PropI32Array(m.polygonIndices);w.End();
    w.Begin("Edges");w.PropI32Array(m.edges);w.End();
    if(m.smoothingMode==0){
        w.Begin("LayerElementNormal");w.PropI32(0);w.Begin("Version");w.PropI32(101);w.End();w.Begin("Name");w.PropStr("");w.End();w.Begin("MappingInformationType");w.PropStr("ByPolygonVertex");w.End();w.Begin("ReferenceInformationType");w.PropStr("Direct");w.End();w.Begin("Normals");w.PropF64Array(m.normals);w.End();w.End(true);
    }else if(m.smoothingMode&2){
        // Smoothing-group data present: write ByPolygon 32-bit group bitmasks,
        // which 3ds Max imports directly as smoothing groups.
        w.Begin("LayerElementSmoothing");w.PropI32(0);w.Begin("Version");w.PropI32(101);w.End();w.Begin("Name");w.PropStr("");w.End();w.Begin("MappingInformationType");w.PropStr("ByPolygon");w.End();w.Begin("ReferenceInformationType");w.PropStr("Direct");w.End();w.Begin("Smoothing");w.PropI32Array(m.faceMasks);w.End();w.End(true);
    }else{
        // Crease-only meshes: ByEdge 0/1 hard-edge flags (Blender sharp edges).
        w.Begin("LayerElementSmoothing");w.PropI32(0);w.Begin("Version");w.PropI32(101);w.End();w.Begin("Name");w.PropStr("");w.End();w.Begin("MappingInformationType");w.PropStr("ByEdge");w.End();w.Begin("ReferenceInformationType");w.PropStr("Direct");w.End();w.Begin("Smoothing");w.PropI32Array(m.smoothing);w.End();w.End(true);
    }
    for(size_t i=0;i<m.uvSets.size();i++){
        const UVSetData &u=m.uvSets[i];w.Begin("LayerElementUV");w.PropI32((int)i);w.Begin("Version");w.PropI32(101);w.End();w.Begin("Name");w.PropStr(u.name);w.End();w.Begin("MappingInformationType");w.PropStr("ByPolygonVertex");w.End();w.Begin("ReferenceInformationType");w.PropStr("IndexToDirect");w.End();w.Begin("UV");w.PropF64Array(u.uv);w.End();w.Begin("UVIndex");w.PropI32Array(u.indices);w.End();w.End(true);
    }
    w.Begin("LayerElementMaterial");w.PropI32(0);w.Begin("Version");w.PropI32(101);w.End();w.Begin("Name");w.PropStr("");w.End();w.Begin("MappingInformationType");w.PropStr("ByPolygon");w.End();w.Begin("ReferenceInformationType");w.PropStr("IndexToDirect");w.End();w.Begin("Materials");w.PropI32Array(m.faceMaterials);w.End();w.End(true);
    w.Begin("Layer");w.PropI32(0);w.Begin("Version");w.PropI32(100);w.End();
    if(m.smoothingMode==0){w.Begin("LayerElement");w.Begin("Type");w.PropStr("LayerElementNormal");w.End();w.Begin("TypedIndex");w.PropI32(0);w.End();w.End(true);}
    else{w.Begin("LayerElement");w.Begin("Type");w.PropStr("LayerElementSmoothing");w.End();w.Begin("TypedIndex");w.PropI32(0);w.End();w.End(true);}
    for(size_t i=0;i<m.uvSets.size();i++){w.Begin("LayerElement");w.Begin("Type");w.PropStr("LayerElementUV");w.End();w.Begin("TypedIndex");w.PropI32((int)i);w.End();w.End(true);}
    w.Begin("LayerElement");w.Begin("Type");w.PropStr("LayerElementMaterial");w.End();w.Begin("TypedIndex");w.PropI32(0);w.End();w.End(true);
    w.End(true);w.End(true);
}

static void WriteMaterial(FbxWriter &w,const MaterialData &md){
    MQMaterial mat=md.material;MQColor diffuse(0.8f,0.8f,0.8f),ambient(0.2f,0.2f,0.2f),emission(0,0,0),specular(0.2f,0.2f,0.2f);
    double df=1,af=1,ef=1,sf=1,shininess=20,transparency=0;
    if(mat){diffuse=mat->GetColor();ambient=mat->GetAmbientColor();emission=mat->GetEmissionColor();specular=mat->GetSpecularColor();df=mat->GetDiffuse();af=mat->GetAmbient();ef=mat->GetEmission();sf=mat->GetSpecular();shininess=mat->GetPower();transparency=1.0-Clamp01(mat->GetAlpha());}
    w.Begin("Material");w.PropI64(md.id);w.PropStr(ObjName(md.name,"Material"));w.PropStr("");w.Begin("Version");w.PropI32(102);w.End();w.Begin("ShadingModel");w.PropStr("Phong");w.End();w.Begin("MultiLayer");w.PropBool(false);w.End();
    w.Begin("Properties70");
    PString(w,"ShadingModel","Phong");PBool(w,"MultiLayer",false);
    PColor(w,"EmissiveColor",emission.r,emission.g,emission.b);PScalar(w,"EmissiveFactor","Number","","A",ef);
    PColor(w,"AmbientColor",ambient.r,ambient.g,ambient.b);PScalar(w,"AmbientFactor","Number","","A",af);
    PColor(w,"DiffuseColor",diffuse.r,diffuse.g,diffuse.b);PScalar(w,"DiffuseFactor","Number","","A",df);
    PColor(w,"TransparentColor",0,0,0);PScalar(w,"TransparencyFactor","Number","","A",transparency);PScalar(w,"Opacity","Number","","A",1.0-transparency);
    PVectorMeta(w,"NormalMap","Vector3D","Vector","",0,0,0);PVectorMeta(w,"Bump","Vector3D","Vector","",0,0,0);
    PScalar(w,"BumpFactor","double","Number","",1);PColorRGB(w,"DisplacementColor",0,0,0);PScalar(w,"DisplacementFactor","double","Number","",1);
    PColorRGB(w,"VectorDisplacementColor",0,0,0);PScalar(w,"VectorDisplacementFactor","double","Number","",1);
    PColor(w,"SpecularColor",specular.r,specular.g,specular.b);PScalar(w,"SpecularFactor","Number","","A",sf);
    PScalar(w,"Shininess","Number","","A",shininess);PScalar(w,"ShininessExponent","Number","","A",shininess);
    PColor(w,"ReflectionColor",0,0,0);PScalar(w,"ReflectionFactor","Number","","A",mat?mat->GetReflection():0);
    w.End(true);
    w.End(true);
}

class ExportFBXNativePlugin : public MQExportPlugin {
public:
    virtual void GetPlugInID(DWORD *Product,DWORD *ID){*Product=0x4642584E;*ID=0x59D21C4B;}
    virtual const char *GetPlugInName(void){return "FBX Export (native) v2.3 (Max-compatible smoothing groups)";}
    virtual const char *EnumFileType(int i){return i==0?"FBX (binary, native)":NULL;}
    virtual const char *EnumFileExt(int i){return i==0?"fbx":NULL;}
    virtual BOOL ExportFile(int,const wchar_t *filename,MQDocument doc){return WriteFBX(filename,doc)?TRUE:FALSE;}
private:
    bool WriteFBX(const wchar_t *path,MQDocument doc);
};

bool ExportFBXNativePlugin::WriteFBX(const wchar_t *path,MQDocument doc){
    FbxExportLog log(path);
    if(!doc){log.Line(L"ERROR: null MQDocument");return false;}
    std::vector<MeshData> objects;std::set<int> usedMaterials;bool needFallback=false;
    int skippedInvisible=0,nullObjectSlots=0,frozenCount=0;
    const int objectCount=doc->GetObjectCount();
    for(int i=0;i<objectCount;i++){
        MQObject obj=doc->GetObject(i);if(!obj){nullObjectSlots++;continue;}if(!obj->GetVisible()){skippedInvisible++;continue;}
        MQObject geometryObj=obj;MQObject frozen=NULL;
        // Freeze virtual patch/mirror/lathe on a temporary clone; never mutate the source document.
        if(obj->GetPatchType()!=MQOBJECT_PATCH_NONE||obj->GetMirrorType()!=MQOBJECT_MIRROR_NONE||obj->GetLatheType()!=MQOBJECT_LATHE_NONE){
            frozen=obj->Clone();if(!frozen){log.Line(L"ERROR: cannot clone object index %d for virtual-mesh freeze",i);return false;}
            if(!frozen->Freeze(MQOBJECT_FREEZE_ALL)){frozen->DeleteThis();log.Line(L"ERROR: virtual-mesh freeze failed for object index %d",i);return false;}
            if(obj->GetPatchType()!=MQOBJECT_PATCH_NONE)PropagatePatchData(obj,frozen,doc);
            geometryObj=frozen;frozenCount++;
        }
        MeshData m;bool meshOK=BuildMeshData(doc,obj,geometryObj,i,m);if(frozen)frozen->DeleteThis();if(!meshOK){log.Line(L"ERROR: invalid mesh data in object index %d",i);return false;}
        if(m.hasMesh){for(size_t k=0;k<m.usedMaterials.size();k++){if(m.usedMaterials[k]<0)needFallback=true;else usedMaterials.insert(m.usedMaterials[k]);}}
        objects.push_back(m);
    }
    std::vector<MaterialData> materials;int matCount=doc->GetMaterialCount();
    for(std::set<int>::const_iterator it=usedMaterials.begin();it!=usedMaterials.end();++it){if(*it<0||*it>=matCount)continue;MaterialData md;md.sourceIndex=*it;md.material=doc->GetMaterial(*it);md.name=md.material?SafeName(md.material->GetNameW(),("Material"+std::to_string(*it)).c_str()):("Material"+std::to_string(*it));materials.push_back(md);}
    if(needFallback){MaterialData md;md.sourceIndex=-1;md.name="_Unattached";materials.push_back(md);}
    std::map<int,int> materialSlot;for(size_t i=0;i<materials.size();i++)materialSlot[materials[i].sourceIndex]=(int)i;
    // FBX polygon material values are local indices into the materials connected to that Model.
    for(size_t oi=0;oi<objects.size();oi++){std::map<int,int> localSlots;for(size_t k=0;k<objects[oi].usedMaterials.size();k++)localSlots[objects[oi].usedMaterials[k]]=(int)k;for(size_t f=0;f<objects[oi].faceMaterials.size();f++){std::map<int,int>::const_iterator it=localSlots.find(objects[oi].faceMaterials[f]);if(it==localSlots.end()){log.Line(L"ERROR: material slot mapping failed for object index %d",objects[oi].sourceIndex);return false;}objects[oi].faceMaterials[f]=it->second;}}
    int64_t nextId=1000000;int geometryCount=0,nullCount=0;
    std::map<MQObject,size_t> objectIndex;
    for(size_t i=0;i<objects.size();i++){objects[i].modelId=nextId++;objectIndex[objects[i].obj]=i;if(objects[i].hasMesh){objects[i].geomId=nextId++;geometryCount++;}else{objects[i].attributeId=nextId++;nullCount++;}}
    for(size_t i=0;i<materials.size();i++)materials[i].id=nextId++;
    // Texture slots: resolve image paths, deduplicate Video nodes by absolute path,
    // and assign Texture/Video node IDs.
    static const char *slotNames[3]={"DiffuseColor","TransparentColor","Bump"};
    struct VideoRec{int64_t id;std::string nodeName,absPath,relPath;};
    std::vector<VideoRec> videos;std::map<std::wstring,size_t> videoByPath;size_t textureCount=0;
    for(size_t i=0;i<materials.size();i++){
        MQMaterial mat=materials[i].material;if(!mat)continue;
        FillTextureSlot(doc,path,mat->GetTextureNameW(),mat->GetTextureUVChannel(),materials[i].diffuse);
        FillTextureSlot(doc,path,mat->GetAlphaNameW(),mat->GetAlphaUVChannel(),materials[i].alpha);
        FillTextureSlot(doc,path,mat->GetBumpNameW(),mat->GetBumpUVChannel(),materials[i].bump);
        TextureSlotData *slots[3]={&materials[i].diffuse,&materials[i].alpha,&materials[i].bump};
        for(int s=0;s<3;s++){
            TextureSlotData *t=slots[s];if(!t->Valid())continue;
            t->textureId=nextId++;textureCount++;
            std::map<std::wstring,size_t>::iterator vi=videoByPath.find(t->absolute);
            if(vi==videoByPath.end()){
                VideoRec vr;vr.id=nextId++;
                std::wstring base=t->absolute;size_t bs=base.find_last_of(L"\\/");
                if(bs!=std::wstring::npos)base=base.substr(bs+1);
                vr.nodeName=MQEncoding::WideToUtf8(base.c_str());
                if(vr.nodeName.empty())vr.nodeName="Video"+std::to_string(videos.size());
                vr.absPath=MQEncoding::WideToUtf8(t->absolute.c_str());
                vr.relPath=MQEncoding::WideToUtf8(t->relative.c_str());
                videoByPath[t->absolute]=videos.size();videos.push_back(vr);
                t->videoId=vr.id;t->videoNodeName=vr.nodeName;
            }else{
                const VideoRec &vr=videos[vi->second];
                t->videoId=vr.id;t->videoNodeName=vr.nodeName;
            }
        }
    }
    FbxWriter w;if(!w.Open(path)){log.Line(L"ERROR: cannot open output file");return false;}
    SYSTEMTIME now;GetSystemTime(&now);
    w.Begin("FBXHeaderExtension");w.Begin("FBXHeaderVersion");w.PropI32(1003);w.End();w.Begin("FBXVersion");w.PropI32(7400);w.End();w.Begin("EncryptionType");w.PropI32(0);w.End();
    w.Begin("CreationTimeStamp");w.Begin("Version");w.PropI32(1000);w.End();w.Begin("Year");w.PropI32(now.wYear);w.End();w.Begin("Month");w.PropI32(now.wMonth);w.End();w.Begin("Day");w.PropI32(now.wDay);w.End();w.Begin("Hour");w.PropI32(now.wHour);w.End();w.Begin("Minute");w.PropI32(now.wMinute);w.End();w.Begin("Second");w.PropI32(now.wSecond);w.End();w.Begin("Millisecond");w.PropI32(now.wMilliseconds);w.End();w.End(true);
    w.Begin("Creator");w.PropStr("ExportFBXNative v2.3");w.End();w.End(true);
    unsigned char fid[16]={0};FILETIME ft;GetSystemTimeAsFileTime(&ft);memcpy(fid,&ft,8);DWORD pid=GetCurrentProcessId(),tick=GetTickCount();memcpy(fid+8,&pid,4);memcpy(fid+12,&tick,4);std::vector<unsigned char> fidv(fid,fid+16);
    char ct[64];sprintf(ct,"%04u-%02u-%02u %02u:%02u:%02u:%03u",(unsigned)now.wYear,(unsigned)now.wMonth,(unsigned)now.wDay,(unsigned)now.wHour,(unsigned)now.wMinute,(unsigned)now.wSecond,(unsigned)now.wMilliseconds);
    w.Begin("FileId");w.PropRaw(fidv);w.End();w.Begin("CreationTime");w.PropStr(ct);w.End();w.Begin("Creator");w.PropStr("ExportFBXNative v2.3");w.End();
    w.Begin("GlobalSettings");w.Begin("Version");w.PropI32(1000);w.End();w.Begin("Properties70");
    PInt(w,"UpAxis",1);PInt(w,"UpAxisSign",1);PInt(w,"FrontAxis",2);PInt(w,"FrontAxisSign",1);PInt(w,"CoordAxis",0);PInt(w,"CoordAxisSign",1);PInt(w,"OriginalUpAxis",-1);PInt(w,"OriginalUpAxisSign",1);
    PScalar(w,"UnitScaleFactor","double","Number","",100.0);PScalar(w,"OriginalUnitScaleFactor","double","Number","",100.0);
    PColorRGB(w,"AmbientColor",0,0,0);PString(w,"DefaultCamera","Producer Perspective");PEnum(w,"TimeMode",11);
    PBegin(w,"TimeSpanStart","KTime","Time","");w.PropI64(0);PEnd(w);PBegin(w,"TimeSpanStop","KTime","Time","");w.PropI64(46186158000LL);PEnd(w);
    PScalar(w,"CustomFrameRate","double","Number","",24.0);
    w.End(true);w.End(true);
    w.Begin("Documents");w.Begin("Count");w.PropI32(1);w.End();w.Begin("Document");w.PropI64(822575237);w.PropStr("Scene");w.PropStr("Scene");w.Begin("Properties70");w.End(true);w.Begin("RootNode");w.PropI64(0);w.End();w.End(true);w.End(true);
    w.Begin("References");w.End();
    w.Begin("Definitions");w.Begin("Version");w.PropI32(100);w.End();w.Begin("Count");w.PropI32(1+(int)objects.size()+geometryCount+nullCount+(int)materials.size()+(int)textureCount+(int)videos.size());w.End();
    w.Begin("ObjectType");w.PropStr("GlobalSettings");w.Begin("Count");w.PropI32(1);w.End();w.End(true);
    if(nullCount){w.Begin("ObjectType");w.PropStr("NodeAttribute");w.Begin("Count");w.PropI32(nullCount);w.End();WriteNullTemplate(w);w.End(true);}
    w.Begin("ObjectType");w.PropStr("Geometry");w.Begin("Count");w.PropI32(geometryCount);w.End();if(geometryCount)WriteGeometryTemplate(w);w.End(true);
    w.Begin("ObjectType");w.PropStr("Model");w.Begin("Count");w.PropI32((int)objects.size());w.End();if(!objects.empty())WriteNodeTemplate(w);w.End(true);
    if(!materials.empty()){w.Begin("ObjectType");w.PropStr("Material");w.Begin("Count");w.PropI32((int)materials.size());w.End();WriteMaterialTemplate(w);w.End(true);}
    if(textureCount){w.Begin("ObjectType");w.PropStr("Texture");w.Begin("Count");w.PropI32((int)textureCount);w.End();WriteTextureTemplate(w);w.End(true);}
    if(!videos.empty()){w.Begin("ObjectType");w.PropStr("Video");w.Begin("Count");w.PropI32((int)videos.size());w.End();WriteVideoTemplate(w);w.End(true);}
    w.End(true);
    w.Begin("Objects");
    for(size_t i=0;i<objects.size();i++)if(objects[i].hasMesh)WriteGeometry(w,objects[i]);
    for(size_t i=0;i<objects.size();i++)if(!objects[i].hasMesh){w.Begin("NodeAttribute");w.PropI64(objects[i].attributeId);w.PropStr(ObjName(objects[i].name,"NodeAttribute"));w.PropStr("Null");w.Begin("TypeFlags");w.PropStr("Null");w.End();w.Begin("Properties70");w.End(true);w.End(true);}
    for(size_t i=0;i<objects.size();i++){
        const MeshData &m=objects[i];w.Begin("Model");w.PropI64(m.modelId);w.PropStr(ObjName(m.name,"Model"));w.PropStr(m.hasMesh?"Mesh":"Null");w.Begin("Version");w.PropI32(232);w.End();WriteModelProperties(w,m);w.Begin("MultiLayer");w.PropI32(0);w.End();w.Begin("MultiTake");w.PropI32(0);w.End();w.Begin("Shading");w.PropBool(true);w.End();w.Begin("Culling");w.PropStr("CullingOff");w.End();w.End(true);
    }
    for(size_t i=0;i<materials.size();i++)WriteMaterial(w,materials[i]);
    for(size_t i=0;i<videos.size();i++)WriteVideo(w,videos[i].id,videos[i].nodeName,videos[i].absPath,videos[i].relPath);
    for(size_t i=0;i<materials.size();i++){
        const TextureSlotData *slots[3]={&materials[i].diffuse,&materials[i].alpha,&materials[i].bump};
        for(int s=0;s<3;s++)if(slots[s]->Valid())WriteTexture(w,*slots[s],materials[i].name+"_"+slotNames[s]);
    }
    w.End(true);
    w.Begin("Connections");
    for(size_t i=0;i<objects.size();i++)if(objects[i].hasMesh){w.Begin("C");w.PropStr("OO");w.PropI64(objects[i].geomId);w.PropI64(objects[i].modelId);w.End();}else{w.Begin("C");w.PropStr("OO");w.PropI64(objects[i].attributeId);w.PropI64(objects[i].modelId);w.End();}
    for(size_t i=0;i<objects.size();i++){
        const MeshData &m=objects[i];if(!m.hasMesh)continue;
        for(size_t k=0;k<m.usedMaterials.size();k++){
            int src=m.usedMaterials[k];std::map<int,int>::const_iterator slot=materialSlot.find(src);if(slot==materialSlot.end())continue;
            w.Begin("C");w.PropStr("OO");w.PropI64(materials[(size_t)slot->second].id);w.PropI64(m.modelId);w.End();
        }
    }
    for(size_t i=0;i<materials.size();i++){
        const TextureSlotData *slots[3]={&materials[i].diffuse,&materials[i].alpha,&materials[i].bump};
        for(int s=0;s<3;s++){
            if(!slots[s]->Valid())continue;
            w.Begin("C");w.PropStr("OP");w.PropI64(slots[s]->textureId);w.PropI64(materials[i].id);w.PropStr(slotNames[s]);w.End();
            w.Begin("C");w.PropStr("OO");w.PropI64(slots[s]->videoId);w.PropI64(slots[s]->textureId);w.End();
        }
    }
    for(size_t i=0;i<objects.size();i++){
        MQObject parent=doc->GetParentObject(objects[i].obj);int64_t parentId=0;
        while(parent){std::map<MQObject,size_t>::const_iterator it=objectIndex.find(parent);if(it!=objectIndex.end()){parentId=objects[it->second].modelId;break;}parent=doc->GetParentObject(parent);}
        w.Begin("C");w.PropStr("OO");w.PropI64(objects[i].modelId);w.PropI64(parentId);w.End();
    }
    w.End(true);w.Begin("Takes");w.Begin("Current");w.PropStr("");w.End();w.End(true);
    size_t polygonTotal=0,cornerTotal=0,uvSetTotal=0,explicitMeshes=0,sgMeshes=0,smoothEdges=0,sharpEdges=0;
    for(size_t i=0;i<objects.size();i++){
        polygonTotal+=objects[i].faceMaterials.size();cornerTotal+=objects[i].polygonIndices.size();uvSetTotal+=objects[i].uvSets.size();
        if(objects[i].smoothingMode&1)explicitMeshes++;else if(objects[i].smoothingMode&2)sgMeshes++;
        for(size_t j=0;j<objects[i].smoothing.size();j++){if(objects[i].smoothing[j])smoothEdges++;else sharpEdges++;}
    }
    if(!w.Good()||!w.Flush()){w.Abort();DeleteFileW(path);log.Line(L"ERROR: FBX binary serialization failed (node/array/offset validation)");return false;}
    bool closed=w.Close();if(!closed){DeleteFileW(path);log.Line(L"ERROR: footer or file close failed");return false;}
    log.Line(L"ExportFBXNative v2.2: SUCCESS");
    log.Line(L"Objects: source_slots=%d exported_visible=%d skipped_invisible=%d null_slots=%d mesh=%d null_nodes=%d frozen_virtual_meshes=%d",objectCount,(int)objects.size(),skippedInvisible,nullObjectSlots,geometryCount,nullCount,frozenCount);
    log.Line(L"Geometry: polygons=%llu polygon_vertices=%llu UV_sets=%llu materials=%d",(unsigned long long)polygonTotal,(unsigned long long)cornerTotal,(unsigned long long)uvSetTotal,(int)materials.size());
    log.Line(L"Smoothing: explicit_meshes=%llu SG_meshes=%llu smooth_edges=%llu sharp_edges=%llu (unified: crease marks force sharp, SG masks as base layer)",(unsigned long long)explicitMeshes,(unsigned long long)sgMeshes,(unsigned long long)smoothEdges,(unsigned long long)sharpEdges);
    log.Line(L"Textures: texture_nodes=%llu video_nodes=%llu (external files referenced, not embedded)",(unsigned long long)textureCount,(unsigned long long)videos.size());
    log.Line(L"Coordinate system: Y-up; UnitScaleFactor=100 (meters); local mesh coordinates and normals written directly; local transforms and visible parent hierarchy preserved.");
    log.Line(L"Note: lights/cameras as native FBX attributes, bones/skin, and animation are not exported in this version.");
    return true;
}

MQBasePlugin *GetPluginClass(){static ExportFBXNativePlugin plugin;return &plugin;}
BOOL APIENTRY DllMain(HINSTANCE hinstDLL,DWORD reason,LPVOID){return TRUE;}
