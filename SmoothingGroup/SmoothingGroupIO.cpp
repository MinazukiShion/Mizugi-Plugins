#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string>
#include "MQBasePlugin.h"

#define SG_PRODUCT 0x53473031
#define SG_PLUGIN  0x7C49A2E1
#define SG_IO_PLUGIN 0x7C49A2E2
#define SG_UDATA_NAME "sgmask"
#define SG_EDGE_STATE_NAME "edge_state"

class SmoothingGroupIOPlugin : public MQStationPlugin
{
public:
	virtual void GetPlugInID(DWORD *Product, DWORD *ID) { *Product = SG_PRODUCT; *ID = SG_IO_PLUGIN; }
	virtual const char *GetPlugInName(void) { return "SmoothingGroup IO v1.0"; }
	virtual const wchar_t *EnumString(void) { return L"平滑组数据"; }
	virtual BOOL Initialize() { return TRUE; }
	virtual void Exit() {}
	virtual BOOL Activate(MQDocument, BOOL) { return FALSE; }
	virtual BOOL IsActivated(MQDocument) { return FALSE; }
	virtual bool ExecuteCallback(MQDocument, void*) { return true; }

	virtual void OnNewDocument(MQDocument doc, const char *filename, NEW_DOCUMENT_PARAM& param)
	{
		Restore(doc, param.elem, NULL, NULL);
	}

	virtual void OnInsertDocument(MQDocument doc, const char *filename, INSERT_DOCUMENT_PARAM& param)
	{
		Restore(doc, param.elem, param.srcdoc, &param);
	}

	virtual void OnSaveDocument(MQDocument doc, const char *filename, SAVE_DOCUMENT_PARAM& param)
	{
		if (param.elem == NULL) return;
		int numObj = doc->GetObjectCount();
		int edgeUid = doc->FindObjectUserData(SG_PRODUCT, SG_PLUGIN, SG_EDGE_STATE_NAME);
		if (edgeUid > 0){
			for (int oi = 0; oi < numObj; oi++){
				MQObject obj = doc->GetObject(oi);
				if (obj == NULL) continue;
				char enabled = 0;
				if (!obj->GetUserData(edgeUid, &enabled) || !enabled) continue;
				MQXmlElement child = param.elem->AddChildElement("EdgeState");
				char numbuf[16];
				sprintf(numbuf, "%d", oi);
				child->SetAttribute("obj", numbuf);
			}
		}

		int uid = doc->FindFaceUserData(SG_PRODUCT, SG_PLUGIN, SG_UDATA_NAME);
		if (uid <= 0) return;
		static const char *hex = "0123456789abcdef";
		for (int oi = 0; oi < numObj; oi++){
			MQObject obj = doc->GetObject(oi);
			if (obj == NULL) continue;
			int nf = obj->GetFaceCount();
			if (nf == 0) continue;
			bool any = false;
			std::string data;
			data.reserve(nf * 8);
			for (int fi = 0; fi < nf; fi++){
				unsigned int mask = 0;
				if (obj->GetFacePointCount(fi) > 0) obj->GetFaceUserData(uid, fi, &mask);
				if (mask) any = true;
				for (int k = 7; k >= 0; k--) data += hex[(mask >> (k * 4)) & 0xF];
			}
			if (!any) continue;
			MQXmlElement child = param.elem->AddChildElement("SG");
			char numbuf[16];
			sprintf(numbuf, "%d", oi);
			child->SetAttribute("obj", numbuf);
			child->SetAttribute("data", data.c_str());
		}
	}

private:
	MQObject DestObject(MQDocument doc, MQDocument srcdoc, INSERT_DOCUMENT_PARAM *param, int index)
	{
		if (srcdoc == NULL || param == NULL) return doc->GetObject(index);
		MQObject src = srcdoc->GetObject(index);
		if (src == NULL) return NULL;
		return doc->GetObject(param->GetDestObjectID(src->GetUniqueID()));
	}

	void Restore(MQDocument doc, MQXmlElement elem, MQDocument srcdoc, INSERT_DOCUMENT_PARAM *param)
	{
		int uid = doc->FindFaceUserData(SG_PRODUCT, SG_PLUGIN, SG_UDATA_NAME);
		if (uid <= 0) uid = doc->CreateFaceUserData(SG_PRODUCT, SG_PLUGIN, SG_UDATA_NAME, sizeof(unsigned int));
		int edgeUid = doc->FindObjectUserData(SG_PRODUCT, SG_PLUGIN, SG_EDGE_STATE_NAME);
		if (edgeUid <= 0) edgeUid = doc->CreateObjectUserData(SG_PRODUCT, SG_PLUGIN, SG_EDGE_STATE_NAME, 1);
		if (elem == NULL) return;

		for (MQXmlElement child = elem->FirstChildElement("EdgeState"); child != NULL; child = elem->NextChildElement("EdgeState", child)){
			int oi = atoi(child->GetAttribute("obj").c_str());
			MQObject obj = DestObject(doc, srcdoc, param, oi);
			if (obj == NULL || edgeUid <= 0) continue;
			obj->AllocUserData(edgeUid);
			char enabled = 1;
			obj->SetUserData(edgeUid, &enabled);
		}
		for (MQXmlElement child = elem->FirstChildElement("SG"); child != NULL; child = elem->NextChildElement("SG", child)){
			int oi = atoi(child->GetAttribute("obj").c_str());
			MQObject obj = DestObject(doc, srcdoc, param, oi);
			if (obj == NULL || uid <= 0) continue;
			obj->AllocFaceUserData(uid);
			std::string data = child->GetAttribute("data");
			int nf = obj->GetFaceCount();
			for (int fi = 0; fi < nf; fi++){
				if (obj->GetFacePointCount(fi) == 0 || (size_t)(fi * 8 + 8) > data.size()) break;
				unsigned int mask = (unsigned int)strtoul(data.substr(fi * 8, 8).c_str(), NULL, 16);
				obj->SetFaceUserData(uid, fi, &mask);
			}
		}
	}
};

MQBasePlugin *GetPluginClass()
{
	static SmoothingGroupIOPlugin plugin;
	return &plugin;
}

BOOL APIENTRY DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
