//---------------------------------------------------------------------------
//
//   ImportFBX for Metasequoia  v1.5
//
//     FBX import via ufbx (https://github.com/ufbx/ufbx, MIT).
//     Meshes + scene hierarchy + all UV sets + materials with
//     diffuse/alpha/bump texture paths.
//     Explicit edge smoothing or normal-derived sharp edges are stored as creases.
//     Transforms: Metasequoia's viewport does not evaluate hierarchical
//     object TRS the way FBX/ufbx composes it (children of transformed
//     empties render at the origin), so world transforms are baked into
//     vertices (geometry_to_world) and every object's TRS is identity.
//     Depth hierarchy is kept for object-panel organization only.
//     Install: Plugins\Import\ImportFBX.dll
//
//---------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "MQBasePlugin.h"
#include "ufbx.h"

#define SG_PRODUCT 0x53473031
#define SG_PLUGIN  0x7C49A2E1
#define SG_EDGE_STATE_NAME "edge_state"

static void FBXLog(const char *fmt, ...)
{
	FILE *fp = fopen("D:\\Mizuho\\fbx_debug.log", "a");
	if (!fp) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fputc('\n', fp);
	fclose(fp);
}

static std::wstring Utf8ToWide(const char *s)
{
	if (!s || !*s) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (n <= 1) return L"";
	std::wstring w((size_t)n - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
	return w;
}

class ImportFBXPlugin : public MQImportPlugin
{
public:
	virtual void GetPlugInID(DWORD *Product, DWORD *ID)
	{
		*Product = 0x46425849;
		*ID      = 0x4A3C1187;
	}
	virtual const char *GetPlugInName(void) { return "FBX Import (ufbx) v1.5 (scene, multi-UV, textures, world-baked)"; }
	virtual const char *EnumFileType(int index) { return index == 0 ? "FBX" : NULL; }
	virtual const char *EnumFileExt(int index)
	{
		if (index == 0) return "fbx";
		if (index == 1) return "mqfbx";
		return NULL;
	}
	virtual BOOL ImportFile(int index, const wchar_t *filename, MQDocument doc);
};

struct FBXImportContext {
	MQDocument doc;
	std::map<ufbx_material*, int> materials;
	std::map<std::string, UINT> uvChannelByName; // FBX uv set name -> MQ channel ID
	std::string fbxDir;
	int edgeStateUid = -1;
	int meshCount = 0;
	int nodeCount = 0;
	int uvChannelCount = 0;
	int textureCount = 0;
};

struct FBXEdgeImport {
	int face, edge;
	uint32_t a, b;
	ufbx_vec3 normalA, normalB;
};

static double NormalDot(ufbx_vec3 a, ufbx_vec3 b)
{
	double al = sqrt(a.x*a.x + a.y*a.y + a.z*a.z);
	double bl = sqrt(b.x*b.x + b.y*b.y + b.z*b.z);
	if (al < 1e-12 || bl < 1e-12) return 1.0;
	return (a.x*b.x + a.y*b.y + a.z*b.z) / (al*bl);
}

// Map an FBX uv set name to a document UV channel ID. Channel 0 is the
// default channel written with classic SetFaceCoordinate.
static UINT ChannelForUvSet(FBXImportContext &ctx, const char *name)
{
	std::string key = name ? name : "";
	auto found = ctx.uvChannelByName.find(key);
	if (found != ctx.uvChannelByName.end()) return found->second;
	std::wstring wname = Utf8ToWide(key.c_str());
	UINT id = wname.empty() ? 0 : ctx.doc->GetUVChannelID(wname.c_str());
	if (id == 0 && !wname.empty())
		id = 1000 + (UINT)ctx.uvChannelByName.size(); // fallback if lookup fails
	ctx.uvChannelByName[key] = id;
	return id;
}

static std::wstring TexturePathW(FBXImportContext &ctx, ufbx_texture *tex)
{
	if (!tex) return L"";
	if (tex->absolute_filename.length) return Utf8ToWide(tex->absolute_filename.data);
	if (tex->relative_filename.length){
		std::string rel = tex->relative_filename.data;
		if (rel.size() > 1 && (rel[1] == ':' || rel[0] == '/' || rel[0] == '\\'))
			return Utf8ToWide(rel.c_str());
		return Utf8ToWide((ctx.fbxDir + "\\" + rel).c_str());
	}
	if (tex->filename.length) return Utf8ToWide(tex->filename.data);
	return L"";
}

static int ImportMaterial(FBXImportContext &ctx, ufbx_material *mat)
{
	if (!mat) return -1;
	auto found = ctx.materials.find(mat);
	if (found != ctx.materials.end()) return found->second;
	MQMaterial m = MQ_CreateMaterial();
	if (!m) return -1;
	m->SetName(mat->name.data);
	ufbx_vec4 c = mat->pbr.base_color.value_vec4;
	m->SetColor(MQColor(c.x, c.y, c.z));
	m->SetAlpha(c.w);

	ufbx_texture *diffTex = mat->fbx.diffuse_color.texture;
	if (!diffTex) diffTex = mat->pbr.base_color.texture;
	if (diffTex){
		std::wstring p = TexturePathW(ctx, diffTex);
		if (!p.empty()){
			m->SetTextureName(p.c_str());
			m->SetMappingType(MQMAPPING_TEXTURE);
			ctx.textureCount++;
			if (diffTex->uv_set.length)
				m->SetTextureUVChannel(ChannelForUvSet(ctx, diffTex->uv_set.data));
		}
	}
	ufbx_texture *alphaTex = mat->fbx.transparency_color.texture;
	if (!alphaTex) alphaTex = mat->fbx.transparency_factor.texture;
	if (alphaTex){
		std::wstring p = TexturePathW(ctx, alphaTex);
		if (!p.empty()){
			m->SetAlphaName(p.c_str());
			ctx.textureCount++;
			if (alphaTex->uv_set.length)
				m->SetAlphaUVChannel(ChannelForUvSet(ctx, alphaTex->uv_set.data));
		}
	}
	ufbx_texture *bumpTex = mat->fbx.bump.texture;
	if (!bumpTex) bumpTex = mat->fbx.normal_map.texture;
	if (bumpTex){
		std::wstring p = TexturePathW(ctx, bumpTex);
		if (!p.empty()){
			m->SetBumpName(p.c_str());
			ctx.textureCount++;
			if (bumpTex->uv_set.length)
				m->SetBumpUVChannel(ChannelForUvSet(ctx, bumpTex->uv_set.data));
		}
	}

	int index = ctx.doc->AddMaterial(m);
	ctx.materials[mat] = index;
	return index;
}

struct UVImportChannel {
	const ufbx_uv_set *set;
	UINT channel;
};

// World transforms are baked into vertices; every object's TRS is identity.
// The depth hierarchy is preserved purely for object-panel organization.
static void ImportNode(FBXImportContext &ctx, ufbx_node *node, int depth)
{
	if (!node) return;
	MQObject obj = MQ_CreateObject();
	if (!obj) return;
	obj->SetName(node->name.data);
	obj->SetDepth(depth);
	obj->SetVisible(node->visible ? 15 : 0);
	obj->SetTranslation(MQPoint(0, 0, 0));
	obj->SetScaling(MQPoint(1, 1, 1));
	obj->SetRotation(MQAngle(0, 0, 0));

	ufbx_mesh *mesh = node->mesh;
	if (mesh){
		// Register all UV sets before materials so texture UV bindings match.
		std::vector<UVImportChannel> uvSets;
		for (size_t ui = 0; ui < mesh->uv_sets.count; ui++){
			const ufbx_uv_set *set = &mesh->uv_sets.data[ui];
			if (!set->vertex_uv.exists) continue;
			UVImportChannel ch;
			ch.set = set;
			if (uvSets.empty()){
				ch.channel = 0; // default channel
				if (set->name.length)
					ctx.uvChannelByName[set->name.data] = 0; // first set name aliases default channel
			}else{
				ch.channel = ChannelForUvSet(ctx, set->name.data);
				obj->AddUVChannel(ch.channel);
			}
			uvSets.push_back(ch);
			ctx.uvChannelCount++;
		}

		std::vector<int> matMap(mesh->materials.count, -1);
		for (size_t mi = 0; mi < mesh->materials.count; mi++)
			matMap[mi] = ImportMaterial(ctx, mesh->materials.data[mi]);

		bool hasExplicitSmoothing = mesh->edge_smoothing.count > 0 && mesh->edge_smoothing.count == mesh->edges.count;
		bool hasEdgeState = hasExplicitSmoothing || mesh->vertex_normal.exists;
		std::map<std::pair<uint32_t,uint32_t>, size_t> edgeMap;
		std::map<std::pair<uint32_t,uint32_t>, std::vector<FBXEdgeImport> > edgeIncidents;
		if (hasExplicitSmoothing){
			for (size_t ei = 0; ei < mesh->edges.count; ei++){
				uint32_t a = mesh->edges.data[ei].a, b = mesh->edges.data[ei].b;
				edgeMap[std::make_pair(std::min(a,b),std::max(a,b))] = ei;
			}
		}
		if (hasEdgeState){
			obj->AllocUserData(ctx.edgeStateUid);
			char enabled = 1;
			obj->SetUserData(ctx.edgeStateUid, &enabled);
		}

		int numVerts = (int)mesh->vertices.count;
		for (int vi = 0; vi < numVerts; vi++){
			ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, mesh->vertices.data[vi]);
			obj->AddVertex(MQPoint(p.x, p.y, p.z));
		}

		for (size_t fi = 0; fi < mesh->faces.count; fi++){
			ufbx_face f = mesh->faces.data[fi];
			int n = (int)f.num_indices;
			if (n < 3) continue;
			std::vector<int> idx(n);
			for (int k = 0; k < n; k++)
				idx[n - 1 - k] = (int)mesh->vertex_indices.data[f.index_begin + k];
			int face = obj->AddFace(n, idx.data());
			if (face < 0) continue;

			if (fi < mesh->face_material.count){
				int mi = mesh->face_material.data[fi];
				if (mi >= 0 && mi < (int)matMap.size() && matMap[mi] >= 0)
					obj->SetFaceMaterial(face, matMap[mi]);
			}

			for (int k = 0; k < n; k++){
				size_t corner = f.index_begin + (size_t)(n - 1 - k);
				for (size_t ui = 0; ui < uvSets.size(); ui++){
					ufbx_vec2 uv = ufbx_get_vertex_vec2(&uvSets[ui].set->vertex_uv, corner);
					MQCoordinate coord(uv.x, uv.y);
					if (uvSets[ui].channel == 0)
						obj->SetFaceCoordinate(face, k, coord);
					else
						obj->SetFaceChannelCoordinate(face, k, uvSets[ui].channel, coord);
				}
				if (hasEdgeState){
					int mqEdge = (k - 1 + n) % n;
					uint32_t a = (uint32_t)idx[k];
					size_t nextCorner = f.index_begin + (size_t)((n - k) % n);
					uint32_t b = mesh->vertex_indices.data[nextCorner];
					std::pair<uint32_t,uint32_t> key = std::make_pair(std::min(a,b),std::max(a,b));
					bool smooth = true;
					if (hasExplicitSmoothing){
						auto edge = edgeMap.find(key);
						if (edge != edgeMap.end()) smooth = mesh->edge_smoothing.data[edge->second];
					}else{
						FBXEdgeImport current;
						current.face = face; current.edge = mqEdge; current.a = a; current.b = b;
						current.normalA = ufbx_get_vertex_vec3(&mesh->vertex_normal, corner);
						current.normalB = ufbx_get_vertex_vec3(&mesh->vertex_normal, nextCorner);
						auto &incidents = edgeIncidents[key];
						for (size_t pi = 0; pi < incidents.size(); pi++){
							const FBXEdgeImport &prior = incidents[pi];
							double dotA = a == prior.a ? NormalDot(current.normalA, prior.normalA) : NormalDot(current.normalA, prior.normalB);
							double dotB = b == prior.b ? NormalDot(current.normalB, prior.normalB) : NormalDot(current.normalB, prior.normalA);
							if (dotA < 0.9999 || dotB < 0.9999){ smooth = false; break; }
						}
						for (size_t pi = 0; pi < incidents.size(); pi++)
							if (!smooth) obj->SetFaceEdgeCrease(incidents[pi].face, incidents[pi].edge, 1.0f);
						incidents.push_back(current);
					}
					obj->SetFaceEdgeCrease(face, mqEdge, smooth ? 0.0f : 1.0f);
				}
			}
		}
		obj->UpdateNormal();
		ctx.meshCount++;
	}

	ctx.doc->AddObject(obj);
	obj->SetDepth(depth);
	ctx.nodeCount++;
	for (size_t ci = 0; ci < node->children.count; ci++)
		ImportNode(ctx, node->children.data[ci], depth + 1);
}

BOOL ImportFBXPlugin::ImportFile(int index, const wchar_t *filename, MQDocument doc)
{
	char path[MAX_PATH];
	WideCharToMultiByte(CP_UTF8, 0, filename, -1, path, MAX_PATH, NULL, NULL);

	ufbx_load_opts opts = {0};
	opts.target_unit_meters = 1.0f;
	ufbx_error err;
	ufbx_scene *scene = ufbx_load_file(path, &opts, &err);
	FBXLog("load %s -> scene=%p", path, (void*)scene);
	if (!scene) return FALSE;
	FBXLog("nodes=%d", (int)scene->nodes.count);

	FBXImportContext ctx;
	ctx.doc = doc;
	{
		std::string p = path;
		size_t slash = p.find_last_of("\\/");
		ctx.fbxDir = (slash == std::string::npos) ? "." : p.substr(0, slash);
	}
	ctx.edgeStateUid = doc->FindObjectUserData(SG_PRODUCT, SG_PLUGIN, SG_EDGE_STATE_NAME);
	if (ctx.edgeStateUid <= 0)
		ctx.edgeStateUid = doc->CreateObjectUserData(SG_PRODUCT, SG_PLUGIN, SG_EDGE_STATE_NAME, 1);
	if (scene->root_node){
		for (size_t ni = 0; ni < scene->root_node->children.count; ni++)
			ImportNode(ctx, scene->root_node->children.data[ni], 0);
	}
	FBXLog("imported_nodes=%d imported_meshes=%d docObjects=%d materials=%d uv_layers=%d textures=%d",
	       ctx.nodeCount, ctx.meshCount, doc->GetObjectCount(), (int)ctx.materials.size(),
	       ctx.uvChannelCount, ctx.textureCount);
	ufbx_free_scene(scene);
	return ctx.meshCount > 0 ? TRUE : FALSE;
}

//---------------------------------------------------------------------------
MQBasePlugin *GetPluginClass()
{
	static ImportFBXPlugin plugin;
	return &plugin;
}

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID)
{
	return TRUE;
}
