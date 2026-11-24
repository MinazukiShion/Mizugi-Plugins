//---------------------------------------------------------------------------
//
//   SmoothingGroup for Metasequoia  v3.0
//
//     3ds Max style smoothing groups (per Autodesk official docs):
//       - 32 numbered buttons grid; click assigns selected faces to the group
//       - buttons highlight which groups the current selection belongs to
//       - Select By SG / Clear All / Auto Smooth + threshold
//       - crease-based normal preview (compatible with native smoothing)
//       - OBJ export with the chosen mode
//     Tool-internal face selection: LMB toggle / Shift add / Ctrl deselect,
//     RMB stays view-rotate (Metasequoia convention).
//     Group data persists in the document file via the plugin XML element.
//
//---------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <map>
#include "MQBasePlugin.h"
#include "MQWidget.h"

HINSTANCE g_hInstance;

#define SG_PRODUCT 0x53473031
#define SG_PLUGIN  0x7C49A2E1
#define SG_UDATA_NAME "sgmask"
#define SG_MAX_GROUP 32

#define SG_NORMAL_BUILTIN 0
#define SG_NORMAL_SG      1
#define SG_NORMAL_BOTH    2

static void SGLog(const char *fmt, ...)
{
	// Release: no-op. 调试时恢复下面的 fopen 实现。
	(void)fmt;
}

class SGWindow;

class SmoothingGroupPlugin : public MQCommandPlugin
{
public:
	SmoothingGroupPlugin();

	virtual void GetPlugInID(DWORD *Product, DWORD *ID);
	virtual const char *GetPlugInName(void);
	virtual const wchar_t *EnumString(void);
	virtual BOOL Initialize();
	virtual void Exit();
	virtual BOOL Activate(MQDocument doc, BOOL flag);

	virtual void OnNewDocument(MQDocument doc, const char *filename, NEW_DOCUMENT_PARAM& param);
	virtual void OnEndDocument(MQDocument doc);
	virtual void OnSaveDocument(MQDocument doc, const char *filename, SAVE_DOCUMENT_PARAM& param);
	virtual void OnObjectSelected(MQDocument doc);

	virtual BOOL OnLeftButtonDown(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state);
	virtual BOOL OnLeftButtonMove(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state);
	virtual BOOL OnLeftButtonUp(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state);
	virtual BOOL OnRightButtonDown(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state);
	virtual BOOL OnMouseMove(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state);
	virtual void OnDraw(MQDocument doc, MQScene scene, int width, int height);
	virtual BOOL OnMiddleButtonDown(MQDocument, MQScene, MOUSE_BUTTON_STATE&) { return FALSE; }
	virtual BOOL OnMiddleButtonMove(MQDocument, MQScene, MOUSE_BUTTON_STATE&) { return FALSE; }
	virtual BOOL OnMiddleButtonUp(MQDocument, MQScene, MOUSE_BUTTON_STATE&)   { return FALSE; }
	virtual BOOL OnRightButtonMove(MQDocument, MQScene, MOUSE_BUTTON_STATE&)  { return FALSE; }
	virtual BOOL OnRightButtonUp(MQDocument, MQScene, MOUSE_BUTTON_STATE&)    { return FALSE; }
	virtual BOOL OnMouseWheel(MQDocument, MQScene, MOUSE_BUTTON_STATE&)       { return FALSE; }
	virtual BOOL OnKeyDown(MQDocument, MQScene, int, MOUSE_BUTTON_STATE&)     { return FALSE; }
	virtual BOOL OnKeyUp(MQDocument, MQScene, int, MOUSE_BUTTON_STATE&)       { return FALSE; }

	int  EnsureUserData(MQDocument doc);
	bool FaceSelected(MQDocument doc, int oi, MQObject obj, int fi);
	void AssignGroup(MQDocument doc, int group, bool add);
	void ToggleAssignGroup(MQDocument doc, int group);
	void ClearGroup(MQDocument doc, bool selectedOnly);
	void SelectByGroup(MQDocument doc, int group);
	void AutoGroup(MQDocument doc, float angleDeg);
	void Colorize(MQDocument doc, bool enable, const std::vector<char>* onlyObjs = nullptr);
	void CreasePreview(MQDocument doc, int mode, float angleDeg, const std::vector<char>* onlyObjs = nullptr);
	void ExportOBJ(MQDocument doc, int mode, float angleDeg);
	void RefreshGroupList();

	int m_UserDataID;
	float m_Angle;
	int m_PreviewMode = -1;  // -1 = 原生，其余见 SG_NORMAL_*
	bool m_ColorEnabled = false;
	std::vector<char> m_ObjDirty;
	MQDocument m_Doc;
	SGWindow *m_Window;

	int m_HoverObject, m_HoverFace;
	int m_DragObject, m_DragFace;
	int m_DragMode;  // 0=toggle 1=add 2=remove
	bool m_ListDirty = false;
	bool m_SelDirty = false;

	bool HitFace(MQScene scene, POINT pt, int& outObj, int& outFace);
	void ToggleFace(MQDocument doc, int oi, int fi, int mode);
};

//---------------------------------------------------------------------------
// helpers
//---------------------------------------------------------------------------
static void FaceNormal(MQObject obj, int fi, const std::vector<int>& vidx, float out[3])
{
	MQPoint p0 = obj->GetVertex(vidx[0]);
	MQPoint p1 = obj->GetVertex(vidx[1]);
	MQPoint p2 = obj->GetVertex(vidx[2]);
	float ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
	float vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
	// 水杉的法线约定与右手叉积相反（实测读回确认）
	out[0] = -(uy * vz - uz * vy);
	out[1] = -(uz * vx - ux * vz);
	out[2] = -(ux * vy - uy * vx);
	float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (len > 1e-12f) { out[0] /= len; out[1] /= len; out[2] /= len; }
}

static unsigned int GroupColor(int group)  // ARGB
{
	float h = fmodf((group - 1) * 47.0f, 360.0f) / 60.0f;
	float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
	float r = 0, g = 0, b = 0;
	if      (h < 1) { r = 1; g = x; }
	else if (h < 2) { r = x; g = 1; }
	else if (h < 3) { g = 1; b = x; }
	else if (h < 4) { g = x; b = 1; }
	else if (h < 5) { r = x; b = 1; }
	else            { r = 1; b = x; }
	unsigned int R = (unsigned int)(r * 255), G = (unsigned int)(g * 255), B = (unsigned int)(b * 255);
	return 0xFF000000 | (R << 16) | (G << 8) | B;
}

// per-object mesh analysis shared by preview/export
struct ObjInfo {
	std::vector< std::vector<int> > fverts;
	std::vector< std::vector<float> > fnorm;
	std::vector<unsigned int> fmask;
	std::map<unsigned long long, std::vector< std::pair<int,int> > > edgeFaces; // edge -> [(face, edgeIdx)]
};

static void AnalyzeObject(MQObject obj, int uid, ObjInfo& info)
{
	int nf = obj->GetFaceCount();
	info.fverts.resize(nf);
	info.fnorm.assign(nf, std::vector<float>(3, 0.f));
	info.fmask.assign(nf, 0u);
	for (int fi = 0; fi < nf; fi++){
		int nv = obj->GetFacePointCount(fi);
		if (nv < 3){ info.fverts[fi].clear(); continue; }
		info.fverts[fi].resize(nv);
		obj->GetFacePointArray(fi, info.fverts[fi].data());
		FaceNormal(obj, fi, info.fverts[fi], info.fnorm[fi].data());
		if (uid > 0) obj->GetFaceUserData(uid, fi, &info.fmask[fi]);
		for (int i = 0; i < nv; i++){
			int a = info.fverts[fi][i], b = info.fverts[fi][(i + 1) % nv];
			unsigned long long key = a < b ?
				((unsigned long long)a << 32 | (unsigned)b) :
				((unsigned long long)b << 32 | (unsigned)a);
			info.edgeFaces[key].push_back(std::make_pair(fi, i));
		}
	}
}

//---------------------------------------------------------------------------
// window
//---------------------------------------------------------------------------
class SGWindow : public MQWindow
{
public:
	SGWindow(int id, SmoothingGroupPlugin *plugin) : MQWindow(id)
	{
		m_Plugin = plugin;

		MQFrame *frame = CreateVerticalFrame(this);
		frame->SetOutSpace(0.2);

		CreateLabel(frame, L"平滑组（选中面后点组号指定）");
		for (int row = 0; row < 4; row++){
			MQFrame *hf = CreateHorizontalFrame(frame);
			hf->SetUniformSize(true);
			for (int col = 0; col < 8; col++){
				int g = row * 8 + col + 1;
				wchar_t buf[8];
				swprintf(buf, 8, L"%d", g);
				m_BtnGroup[g - 1] = CreateButton(hf, buf);
				m_BtnGroup[g - 1]->AddClickEvent(this, &SGWindow::OnGroup);
			}
		}

		MQFrame *row1 = CreateHorizontalFrame(frame);
		m_BtnSelect   = CreateButton(row1, L"按组选择");
		m_BtnClearSel = CreateButton(row1, L"清除选中");
		m_BtnClearAll = CreateButton(row1, L"全部清除");

		MQFrame *row4 = CreateHorizontalFrame(frame);
		CreateLabel(row4, L"角度");
		m_AngleSpin = CreateSpinBox(row4);
		m_AngleSpin->SetMin(1);
		m_AngleSpin->SetMax(180);
		m_AngleSpin->SetPosition(45);
		m_AngleSpin->AddChangedEvent(this, &SGWindow::OnAngleChanged);
		m_BtnAuto = CreateButton(row4, L"自动分组");

		MQFrame *row5 = CreateHorizontalFrame(frame);
		m_BtnColor   = CreateButton(row5, L"按组上色");
		m_BtnUncolor = CreateButton(row5, L"清除颜色");

		CreateLabel(frame, L"法线预览（折痕方式）");
		MQFrame *row6 = CreateHorizontalFrame(frame);
		m_BtnNrmBuiltin = CreateButton(row6, L"自带法线");
		m_BtnNrmSG      = CreateButton(row6, L"平滑组");
		m_BtnNrmBoth    = CreateButton(row6, L"组合");

		MQFrame *row7 = CreateHorizontalFrame(frame);
		m_ExportMode = CreateComboBox(row7);
		m_ExportMode->AddItem(L"自带法线");
		m_ExportMode->AddItem(L"平滑组法线");
		m_ExportMode->AddItem(L"组合法线");
		m_ExportMode->SetCurrentIndex(SG_NORMAL_SG);
		m_BtnExport = CreateButton(row7, L"导出 OBJ");

		m_BtnSelect->AddClickEvent(this, &SGWindow::OnSelect);
		m_BtnClearSel->AddClickEvent(this, &SGWindow::OnClearSel);
		m_BtnClearAll->AddClickEvent(this, &SGWindow::OnClearAll);
		m_BtnAuto->AddClickEvent(this, &SGWindow::OnAuto);
		m_BtnColor->AddClickEvent(this, &SGWindow::OnColor);
		m_BtnUncolor->AddClickEvent(this, &SGWindow::OnUncolor);
		m_BtnNrmBuiltin->AddClickEvent(this, &SGWindow::OnNrmBuiltin);
		m_BtnNrmSG->AddClickEvent(this, &SGWindow::OnNrmSG);
		m_BtnNrmBoth->AddClickEvent(this, &SGWindow::OnNrmBoth);
		m_BtnExport->AddClickEvent(this, &SGWindow::OnExport);
	}

	SmoothingGroupPlugin *m_Plugin;
	MQButton *m_BtnGroup[SG_MAX_GROUP];
	int m_CurrentGroup = 1;
	MQSpinBox *m_AngleSpin;
	MQComboBox *m_ExportMode;
	MQButton *m_BtnSelect, *m_BtnClearSel, *m_BtnClearAll, *m_BtnAuto;
	MQButton *m_BtnColor, *m_BtnUncolor;
	MQButton *m_BtnNrmBuiltin, *m_BtnNrmSG, *m_BtnNrmBoth, *m_BtnExport;

	BOOL OnGroup(MQWidgetBase *s, MQDocument doc)
	{
		for (int g = 0; g < SG_MAX_GROUP; g++){
			if (m_BtnGroup[g] == s){
				SGLog("BTN group %d t=%u", g + 1, GetTickCount());
				m_CurrentGroup = g + 1;
				m_Plugin->ToggleAssignGroup(doc, g + 1);  // 再点一次=取消归入该组
				return TRUE;
			}
		}
		return TRUE;
	}
	BOOL OnAngleChanged(MQWidgetBase *s, MQDocument doc)
	{
		m_Plugin->m_Angle = (float)m_AngleSpin->GetPosition();
		return TRUE;
	}
	BOOL OnSelect(MQWidgetBase *s, MQDocument doc)  { SGLog("BTN select t=%u", GetTickCount()); m_Plugin->SelectByGroup(doc, CurrentGroup()); return TRUE; }
	BOOL OnClearSel(MQWidgetBase *s, MQDocument doc){ m_Plugin->ClearGroup(doc, true);  return TRUE; }
	BOOL OnClearAll(MQWidgetBase *s, MQDocument doc){ m_Plugin->ClearGroup(doc, false); return TRUE; }
	BOOL OnAuto(MQWidgetBase *s, MQDocument doc)    { SGLog("BTN auto t=%u", GetTickCount()); m_Plugin->AutoGroup(doc, (float)m_AngleSpin->GetPosition()); return TRUE; }
	BOOL OnColor(MQWidgetBase *s, MQDocument doc)   { SGLog("BTN color t=%u", GetTickCount()); m_Plugin->Colorize(doc, true);  return TRUE; }
	BOOL OnUncolor(MQWidgetBase *s, MQDocument doc) { m_Plugin->Colorize(doc, false); return TRUE; }
	BOOL OnNrmBuiltin(MQWidgetBase *s, MQDocument doc) { m_Plugin->m_PreviewMode = SG_NORMAL_BUILTIN; m_Plugin->CreasePreview(doc, SG_NORMAL_BUILTIN, m_Plugin->m_Angle); return TRUE; }
	BOOL OnNrmSG(MQWidgetBase *s, MQDocument doc)      { m_Plugin->m_PreviewMode = SG_NORMAL_SG; m_Plugin->CreasePreview(doc, SG_NORMAL_SG, m_Plugin->m_Angle);      return TRUE; }
	BOOL OnNrmBoth(MQWidgetBase *s, MQDocument doc)    { m_Plugin->m_PreviewMode = SG_NORMAL_BOTH; m_Plugin->CreasePreview(doc, SG_NORMAL_BOTH, m_Plugin->m_Angle);    return TRUE; }
	BOOL OnExport(MQWidgetBase *s, MQDocument doc)     { m_Plugin->ExportOBJ(doc, m_ExportMode->GetCurrentIndex(), m_Plugin->m_Angle); return TRUE; }

	int CurrentGroup()
	{
		return m_CurrentGroup;
	}
};

//---------------------------------------------------------------------------
SmoothingGroupPlugin::SmoothingGroupPlugin()
{
	m_UserDataID = -1;
	m_Angle = 45.0f;
	m_Window = NULL;
	m_HoverObject = m_HoverFace = -1;
	m_DragObject = m_DragFace = -1;
	m_DragMode = 0;
	m_Doc = NULL;
}

void SmoothingGroupPlugin::GetPlugInID(DWORD *Product, DWORD *ID)
{
	*Product = SG_PRODUCT;
	*ID      = SG_PLUGIN;
}

const char *SmoothingGroupPlugin::GetPlugInName(void)
{
	return "Smoothing Group v3.0";
}

const wchar_t *SmoothingGroupPlugin::EnumString(void)
{
	return L"平滑组";
}

BOOL SmoothingGroupPlugin::Initialize()
{
	return TRUE;
}

void SmoothingGroupPlugin::Exit()
{
	delete m_Window;
	m_Window = NULL;
}

BOOL SmoothingGroupPlugin::Activate(MQDocument doc, BOOL flag)
{
	SGLog("Activate flag=%d t=%u", flag, GetTickCount());
	if (flag){
		m_Doc = doc;
		EnsureUserData(doc);
		if (m_Window == NULL){
			m_Window = new SGWindow(MQWindow::GetSystemWidgetID(MQSystemWidget::OptionPanel), this);
			m_Window->SetTitle(L"平滑组");
			POINT size = m_Window->GetJustSize();
			m_Window->SetWidth(size.x);
			m_Window->SetHeight(size.y);
		}
		m_Window->SetVisible(true);
		SetStatusString(L"工具内选择：左键=选择/取消，Shift=加选，Ctrl=减选；点组号指定");
		RefreshGroupList();
	}else{
		// 宿主在停用时可能销毁 OptionPanel 子窗口，必须置空防野指针
		delete m_Window;
		m_Window = NULL;
	}
	return TRUE;
}

int SmoothingGroupPlugin::EnsureUserData(MQDocument doc)
{
	if (m_UserDataID <= 0){
		m_UserDataID = doc->FindFaceUserData(SG_PRODUCT, SG_PLUGIN, SG_UDATA_NAME);
	}
	if (m_UserDataID <= 0){
		m_UserDataID = doc->CreateFaceUserData(SG_PRODUCT, SG_PLUGIN, SG_UDATA_NAME, sizeof(unsigned int));
	}
	return m_UserDataID;
}

bool SmoothingGroupPlugin::FaceSelected(MQDocument doc, int oi, MQObject obj, int fi)
{
	// 严格面级（Max 语义），不做顶点兜底溢出
	return doc->IsSelectFace(oi, fi) != 0;
}

void SmoothingGroupPlugin::RefreshGroupList()
{
	DWORD t0 = GetTickCount();
	if (m_Window == NULL) return;
	MQDocument doc = m_Doc;
	if (doc == NULL || m_UserDataID <= 0) return;
	int counts[SG_MAX_GROUP] = {0};
	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL) continue;
		int nf = obj->GetFaceCount();
		for (int fi = 0; fi < nf; fi++){
			if (obj->GetFacePointCount(fi) == 0) continue;
			if (!FaceSelected(doc, oi, obj, fi)) continue;
			unsigned int mask = 0;
			obj->GetFaceUserData(m_UserDataID, fi, &mask);
			for (int g = 0; g < SG_MAX_GROUP; g++)
				if (mask & (1u << g)) counts[g]++;
		}
	}
	for (int g = 0; g < SG_MAX_GROUP; g++){
		wchar_t buf[16];
		if (counts[g] > 0) swprintf(buf, 16, L"%d●", g + 1);
		else               swprintf(buf, 16, L"%d", g + 1);
		m_Window->m_BtnGroup[g]->SetText(buf);
	}
	SGLog("RefreshGroupList took %u ms", GetTickCount() - t0);
}

void SmoothingGroupPlugin::OnObjectSelected(MQDocument doc)
{
	m_Doc = doc;
	m_SelDirty = true;  // 防抖：事件再多也只在下一帧刷新一次
}

//---------------------------------------------------------------------------
// operations
//---------------------------------------------------------------------------
void SmoothingGroupPlugin::AssignGroup(MQDocument doc, int group, bool add)
{
	DWORD t0 = GetTickCount();
	int uid = EnsureUserData(doc);
	if (uid <= 0) return;
	unsigned int bit = 1u << (group - 1);
	int n = 0;
	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
		obj->AllocFaceUserData(uid);
		int nf = obj->GetFaceCount();
		for (int fi = 0; fi < nf; fi++){
			if (obj->GetFacePointCount(fi) == 0) continue;
			if (!FaceSelected(doc, oi, obj, fi)) continue;
			unsigned int mask = 0;
			if (add) obj->GetFaceUserData(uid, fi, &mask);
			mask |= bit;
			obj->SetFaceUserData(uid, fi, &mask);
			if (oi >= (int)m_ObjDirty.size()) m_ObjDirty.resize(oi + 1, 0);
			m_ObjDirty[oi] = 1;
			n++;
		}
	}
	SGLog("AssignGroup took %u ms", GetTickCount() - t0);
	wchar_t buf[128];
	swprintf(buf, 128, L"%d 个面归入组 %d", n, group);
	SetStatusString(buf);
	RefreshGroupList();
	if (m_ColorEnabled) Colorize(doc, true, &m_ObjDirty);
	if (m_PreviewMode > SG_NORMAL_BUILTIN) CreasePreview(doc, m_PreviewMode, m_Angle, &m_ObjDirty);
	m_ObjDirty.clear();
	RedrawAllScene();
}

void SmoothingGroupPlugin::ToggleAssignGroup(MQDocument doc, int group)
{
	// 若选中面已全部属于该组 -> 再点一次取消归入；否则归入
	int uid = EnsureUserData(doc);
	if (uid <= 0) return;
	unsigned int bit = 1u << (group - 1);
	bool allHave = true;
	bool anySel = false;
	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
		int nf = obj->GetFaceCount();
		for (int fi = 0; fi < nf; fi++){
			if (obj->GetFacePointCount(fi) == 0) continue;
			if (!FaceSelected(doc, oi, obj, fi)) continue;
			anySel = true;
			unsigned int mask = 0;
			obj->GetFaceUserData(uid, fi, &mask);
			if (!(mask & bit)) { allHave = false; break; }
		}
	}
	if (!anySel){
		SetStatusString(L"未选中面");
		return;
	}
	if (allHave){
		for (int oi = 0; oi < numObj; oi++){
			MQObject obj = doc->GetObject(oi);
			if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
			obj->AllocFaceUserData(uid);
			int nf = obj->GetFaceCount();
			for (int fi = 0; fi < nf; fi++){
				if (obj->GetFacePointCount(fi) == 0) continue;
				if (!FaceSelected(doc, oi, obj, fi)) continue;
				unsigned int mask = 0;
				obj->GetFaceUserData(uid, fi, &mask);
				mask &= ~bit;
				obj->SetFaceUserData(uid, fi, &mask);
				if (oi >= (int)m_ObjDirty.size()) m_ObjDirty.resize(oi + 1, 0);
				m_ObjDirty[oi] = 1;
			}
		}
		wchar_t buf[64];
		swprintf(buf, 64, L"选中面已移出组 %d", group);
		SetStatusString(buf);
		RefreshGroupList();
		if (m_ColorEnabled) Colorize(doc, true, &m_ObjDirty);
		if (m_PreviewMode > SG_NORMAL_BUILTIN) CreasePreview(doc, m_PreviewMode, m_Angle, &m_ObjDirty);
		m_ObjDirty.clear();
		RedrawAllScene();
	}else{
		AssignGroup(doc, group, true);
	}
}

void SmoothingGroupPlugin::ClearGroup(MQDocument doc, bool selectedOnly)
{
	int uid = EnsureUserData(doc);
	if (uid <= 0) return;
	unsigned int zero = 0;
	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
		obj->AllocFaceUserData(uid);
		int nf = obj->GetFaceCount();
		for (int fi = 0; fi < nf; fi++){
			if (obj->GetFacePointCount(fi) == 0) continue;
			if (selectedOnly && !FaceSelected(doc, oi, obj, fi)) continue;
			obj->SetFaceUserData(uid, fi, &zero);
			if (oi >= (int)m_ObjDirty.size()) m_ObjDirty.resize(oi + 1, 0);
			m_ObjDirty[oi] = 1;
		}
	}
	RefreshGroupList();
	if (m_ColorEnabled) Colorize(doc, true, &m_ObjDirty);
	if (m_PreviewMode > SG_NORMAL_BUILTIN) CreasePreview(doc, m_PreviewMode, m_Angle, &m_ObjDirty);
	m_ObjDirty.clear();
	RedrawAllScene();
}

void SmoothingGroupPlugin::SelectByGroup(MQDocument doc, int group)
{
	int uid = EnsureUserData(doc);
	if (uid <= 0) return;
	unsigned int bit = 1u << (group - 1);
	doc->ClearSelect(MQDOC_CLEARSELECT_FACE);
	int n = 0;
	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
		int nf = obj->GetFaceCount();
		for (int fi = 0; fi < nf; fi++){
			if (obj->GetFacePointCount(fi) == 0) continue;
			unsigned int mask = 0;
			obj->GetFaceUserData(uid, fi, &mask);
			if (mask & bit){
				doc->AddSelectFace(oi, fi);
				n++;
			}
		}
	}
	wchar_t buf[128];
	swprintf(buf, 128, L"组 %d 共 %d 个面", group, n);
	SetStatusString(buf);
	RefreshGroupList();
	RedrawAllScene();
}

void SmoothingGroupPlugin::AutoGroup(MQDocument doc, float angleDeg)
{
	int uid = EnsureUserData(doc);
	if (uid <= 0) return;
	float cosT = cosf(angleDeg * 3.14159265f / 180.0f);
	int totalGroups = 0;

	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
		obj->AllocFaceUserData(uid);
		ObjInfo info;
		AnalyzeObject(obj, uid, info);
		int nf = (int)info.fverts.size();
		if (nf == 0) continue;

		std::vector<char> visited(nf, 0);
		int group = 0;
		for (int start = 0; start < nf; start++){
			if (visited[start] || info.fverts[start].empty()) continue;
			group++;
			if (group > 31) group = 31;
			unsigned int bit = 1u << (group - 1);
			std::vector<int> stack(1, start);
			visited[start] = 1;
			while (!stack.empty()){
				int f = stack.back(); stack.pop_back();
				obj->SetFaceUserData(uid, f, &bit);
				int nv = (int)info.fverts[f].size();
				for (int i = 0; i < nv; i++){
					int a = info.fverts[f][i], b = info.fverts[f][(i + 1) % nv];
					unsigned long long key = a < b ?
						((unsigned long long)a << 32 | (unsigned)b) :
						((unsigned long long)b << 32 | (unsigned)a);
					for (auto &fc : info.edgeFaces[key]){
						int g = fc.first;
						if (visited[g] || info.fverts[g].empty()) continue;
						float d = info.fnorm[f][0] * info.fnorm[g][0] + info.fnorm[f][1] * info.fnorm[g][1] + info.fnorm[f][2] * info.fnorm[g][2];
						if (d >= cosT){
							visited[g] = 1;
							stack.push_back(g);
						}
					}
				}
			}
		}
		totalGroups += group;
	}
	wchar_t buf[128];
	swprintf(buf, 128, L"自动分组完成：共 %d 组", totalGroups);
	SetStatusString(buf);
	RefreshGroupList();
	Colorize(doc, true);
	if (m_PreviewMode > SG_NORMAL_BUILTIN) CreasePreview(doc, m_PreviewMode, m_Angle);
	RedrawAllScene();
}

void SmoothingGroupPlugin::Colorize(MQDocument doc, bool enable, const std::vector<char>* onlyObjs)
{
	m_ColorEnabled = enable;
	int uid = EnsureUserData(doc);

	int numMat = doc->GetMaterialCount();
	for (int mi = 0; mi < numMat; mi++){
		MQMaterial mat = doc->GetMaterial(mi);
		if (mat) mat->SetVertexColor(enable ? MQMATERIAL_VERTEXCOLOR_DIFFUSE
		                                    : MQMATERIAL_VERTEXCOLOR_DISABLE);
	}

	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		if (onlyObjs && oi < (int)onlyObjs->size() && !(*onlyObjs)[oi]) continue;
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
		if (enable) obj->AddRenderFlag(MQOBJECT_RENDER_VERTEXCOLOR);
		else        obj->RemoveRenderFlag(MQOBJECT_RENDER_VERTEXCOLOR);
		int nf = obj->GetFaceCount();
		for (int fi = 0; fi < nf; fi++){
			int nv = obj->GetFacePointCount(fi);
			if (nv == 0) continue;
			unsigned int color = 0xFFFFFFFF;
			if (enable && uid > 0){
				unsigned int mask = 0;
				obj->GetFaceUserData(uid, fi, &mask);
				if (mask){
					int g = 0;
					while (g < 31 && !(mask & (1u << g))) g++;
					color = GroupColor(g + 1);
				}
			}
			for (int i = 0; i < nv; i++)
				obj->SetFaceVertexColor(fi, i, color);
		}
	}
	RedrawAllScene();
}

//---------------------------------------------------------------------------
// crease preview (native-smoothing compatible)
//---------------------------------------------------------------------------
void SmoothingGroupPlugin::CreasePreview(MQDocument doc, int mode, float angleDeg, const std::vector<char>* onlyObjs)
{
	DWORD t0 = GetTickCount();
	int uid = EnsureUserData(doc);
	float cosT = cosf(angleDeg * 3.14159265f / 180.0f);
	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		if (onlyObjs && oi < (int)onlyObjs->size() && !(*onlyObjs)[oi]) continue;
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible() || obj->GetLocking()) continue;
		ObjInfo info;
		AnalyzeObject(obj, uid, info);
		int nf = (int)info.fverts.size();
		if (nf == 0) continue;

		if (mode == SG_NORMAL_BUILTIN){
			// 清除全部自定义法线，恢复水杉原生自动平滑（不冻结）
			MQPoint zero(0, 0, 0);
			for (int fi = 0; fi < nf; fi++){
				int nv = (int)info.fverts[fi].size();
				for (int i = 0; i < nv; i++)
					obj->SetFaceVertexNormal(fi, i, MQOBJECT_NORMAL_NONE, zero);
			}
			obj->UpdateNormal();
			continue;
		}

		// SG/BOTH：写固定法线做组效果预览
		// 顶点->面邻接表（避免 O(F^2)）
		std::map<int, std::vector<int> > vertFaces;
		for (int fi = 0; fi < nf; fi++)
			for (int v : info.fverts[fi]) vertFaces[v].push_back(fi);

		for (int fi = 0; fi < nf; fi++){
			int nvf = (int)info.fverts[fi].size();
			for (int i = 0; i < nvf; i++){
				float acc[3] = {0, 0, 0};
				for (int f2 : vertFaces[info.fverts[fi][i]]){
					bool ok;
					if (mode == SG_NORMAL_SG){
						ok = (info.fmask[fi] & info.fmask[f2]) != 0 || f2 == fi;
					}else{
						float d = info.fnorm[fi][0] * info.fnorm[f2][0]
						        + info.fnorm[fi][1] * info.fnorm[f2][1]
						        + info.fnorm[fi][2] * info.fnorm[f2][2];
						ok = ((info.fmask[fi] & info.fmask[f2]) != 0 || f2 == fi) && d >= cosT;
					}
					if (ok){
						acc[0] += info.fnorm[f2][0];
						acc[1] += info.fnorm[f2][1];
						acc[2] += info.fnorm[f2][2];
					}
				}
				float len = sqrtf(acc[0]*acc[0] + acc[1]*acc[1] + acc[2]*acc[2]);
				MQPoint n;
				if (len > 1e-12f) n = MQPoint(acc[0]/len, acc[1]/len, acc[2]/len);
				else              n = MQPoint(info.fnorm[fi][0], info.fnorm[fi][1], info.fnorm[fi][2]);
				obj->SetFaceVertexNormal(fi, i, MQOBJECT_NORMAL_FIX, n);
			}
		}
		obj->UpdateNormal();  // 通知渲染器法线已变（否则 GPU 缓存不更新）
	}
	SGLog("CreasePreview took %u ms", GetTickCount() - t0);
	static const wchar_t *names[] = {L"自带法线（原生自动）", L"平滑组（固定法线预览）", L"组合（固定法线预览）"};
	wchar_t buf[128];
	swprintf(buf, 128, L"法线预览：%s", names[mode]);
	SetStatusString(buf);
	RedrawAllScene();
}

//---------------------------------------------------------------------------
// OBJ export (normals computed from mode, not written into the document)
//---------------------------------------------------------------------------
void SmoothingGroupPlugin::ExportOBJ(MQDocument doc, int mode, float angleDeg)
{
	float cosT = cosf(angleDeg * 3.14159265f / 180.0f);
	int uid = EnsureUserData(doc);

	wchar_t path[MAX_PATH] = {0};
	OPENFILENAMEW ofn = {0};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = MQ_GetWindowHandle();
	ofn.lpstrFilter = L"OBJ (*.obj)\0*.obj\0\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"obj";
	ofn.Flags = OFN_OVERWRITEPROMPT;
	if (!GetSaveFileNameW(&ofn)) return;

	FILE *fp = _wfopen(path, L"w");
	if (!fp){
		SetStatusString(L"无法写入文件");
		return;
	}
	fprintf(fp, "# SmoothingGroup OBJ export (mode=%d)\n", mode);

	int vOffset = 1, vnOffset = 1;
	int numObj = doc->GetObjectCount();
	for (int oi = 0; oi < numObj; oi++){
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL || !obj->GetVisible()) continue;
		int nv = obj->GetVertexCount();
		ObjInfo info;
		AnalyzeObject(obj, uid, info);
		int nf = (int)info.fverts.size();

		fprintf(fp, "o object%d\n", oi);
		for (int vi = 0; vi < nv; vi++){
			MQPoint p = obj->GetVertex(vi);
			fprintf(fp, "v %.6f %.6f %.6f\n", p.x, p.y, p.z);
		}

		// corner normals: average faces sharing the vertex unless the edge between
		// them is a boundary for the chosen mode
		std::map<int, std::vector<int> > vertFaces;
		for (int fi = 0; fi < nf; fi++)
			for (int v : info.fverts[fi]) vertFaces[v].push_back(fi);

		for (int fi = 0; fi < nf; fi++){
			int nvf = (int)info.fverts[fi].size();
			for (int i = 0; i < nvf; i++){
				float acc[3] = {0, 0, 0};
				for (int f2 : vertFaces[info.fverts[fi][i]]){
					bool ok;
					if (mode == SG_NORMAL_BUILTIN){
						// 自带法线 = 按角度平滑（facet 语义），不是无脑平均
						float d = info.fnorm[fi][0] * info.fnorm[f2][0]
						        + info.fnorm[fi][1] * info.fnorm[f2][1]
						        + info.fnorm[fi][2] * info.fnorm[f2][2];
						ok = (f2 == fi) || (d >= cosT);
					}else{
						ok = (info.fmask[fi] & info.fmask[f2]) != 0 || f2 == fi;
						if (ok && mode == SG_NORMAL_BOTH){
							float d = info.fnorm[fi][0] * info.fnorm[f2][0]
							        + info.fnorm[fi][1] * info.fnorm[f2][1]
							        + info.fnorm[fi][2] * info.fnorm[f2][2];
							ok = d >= cosT;
						}
					}
					if (ok){
						acc[0] += info.fnorm[f2][0];
						acc[1] += info.fnorm[f2][1];
						acc[2] += info.fnorm[f2][2];
					}
				}
				float len = sqrtf(acc[0]*acc[0] + acc[1]*acc[1] + acc[2]*acc[2]);
				float nx = info.fnorm[fi][0], ny = info.fnorm[fi][1], nz = info.fnorm[fi][2];
				if (len > 1e-12f){ nx = acc[0]/len; ny = acc[1]/len; nz = acc[2]/len; }
				fprintf(fp, "vn %.4f %.4f %.4f\n", nx, ny, nz);
			}
		}

		int vnCur = vnOffset;
		std::vector<int> vidx;
		for (int fi = 0; fi < nf; fi++){
			int nvf = (int)info.fverts[fi].size();
			if (nvf == 0) continue;
			vidx.resize(nvf);
			obj->GetFacePointArray(fi, vidx.data());
			fprintf(fp, "f");
			for (int i = 0; i < nvf; i++)
				fprintf(fp, " %d//%d", vidx[i] + vOffset, vnCur++);
			fprintf(fp, "\n");
		}
		vOffset += nv;
		vnOffset = vnCur;
	}
	fclose(fp);

	wchar_t buf[MAX_PATH + 32];
	swprintf(buf, MAX_PATH + 32, L"已导出：%s", path);
	SetStatusString(buf);
}

//---------------------------------------------------------------------------
// 工具内选择
//---------------------------------------------------------------------------
bool SmoothingGroupPlugin::HitFace(MQScene scene, POINT pt, int& outObj, int& outFace)
{
	HIT_TEST_PARAM ht;
	ht.TestVertex = FALSE;
	ht.TestLine = FALSE;
	ht.TestFace = TRUE;
	if (!HitTest(scene, pt, ht)) return false;
	if (ht.FaceIndex < 0 || ht.ObjectIndex < 0) return false;
	outObj = ht.ObjectIndex;
	outFace = ht.FaceIndex;
	return true;
}

void SmoothingGroupPlugin::ToggleFace(MQDocument doc, int oi, int fi, int mode)
{
	bool sel = doc->IsSelectFace(oi, fi) != 0;
	if (mode == 1){          // add only
		if (!sel) doc->AddSelectFace(oi, fi);
	}else if (mode == 2){    // remove only
		if (sel) doc->DeleteSelectFace(oi, fi);
	}else{                   // toggle
		if (sel) doc->DeleteSelectFace(oi, fi);
		else     doc->AddSelectFace(oi, fi);
	}
}

BOOL SmoothingGroupPlugin::OnLeftButtonDown(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state)
{
	int oi, fi;
	if (!HitFace(scene, state.MousePos, oi, fi)) return TRUE;

	m_DragObject = oi;
	m_DragFace = fi;
	m_DragMode = state.Shift ? 1 : (state.Ctrl ? 2 : 0);

	ToggleFace(doc, oi, fi, m_DragMode);
	m_ListDirty = true;
	RedrawScene(scene);
	return TRUE;
}

BOOL SmoothingGroupPlugin::OnLeftButtonMove(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state)
{
	int oi, fi;
	if (!HitFace(scene, state.MousePos, oi, fi)) return TRUE;
	if (oi == m_DragObject && fi == m_DragFace) return TRUE;

	m_DragObject = oi;
	m_DragFace = fi;
	ToggleFace(doc, oi, fi, m_DragMode);
	m_ListDirty = true;
	RedrawScene(scene);
	return TRUE;
}

BOOL SmoothingGroupPlugin::OnLeftButtonUp(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state)
{
	m_DragObject = m_DragFace = -1;
	if (m_ListDirty){
		m_ListDirty = false;
		RefreshGroupList();
		RedrawAllScene();
	}
	return TRUE;
}

BOOL SmoothingGroupPlugin::OnRightButtonDown(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state)
{
	return FALSE;  // 右键归还视角旋转（水杉惯例）
}

BOOL SmoothingGroupPlugin::OnMouseMove(MQDocument doc, MQScene scene, MOUSE_BUTTON_STATE& state)
{
	int oi, fi;
	bool hit = HitFace(scene, state.MousePos, oi, fi);
	int no = hit ? oi : -1, nf = hit ? fi : -1;
	if (no != m_HoverObject || nf != m_HoverFace){
		m_HoverObject = no;
		m_HoverFace = nf;
		RedrawScene(scene);
	}
	return FALSE;
}

void SmoothingGroupPlugin::OnDraw(MQDocument doc, MQScene scene, int width, int height)
{
	if (m_SelDirty){
		m_SelDirty = false;
		RefreshGroupList();
	}
	if (m_HoverObject < 0 || m_HoverFace < 0) return;
	MQObject obj = doc->GetObject(m_HoverObject);
	if (obj == NULL) return;
	int nv = obj->GetFacePointCount(m_HoverFace);
	if (nv < 3) return;

	std::vector<int> vidx(nv);
	obj->GetFacePointArray(m_HoverFace, vidx.data());
	MQObject draw = CreateDrawingObject(doc, DRAW_OBJECT_LINE);
	if (draw == NULL) return;
	draw->SetColor(MQColor(1.0f, 0.85f, 0.0f));
	draw->SetColorValid(TRUE);
	for (int i = 0; i < nv; i++){
		int a = draw->AddVertex(obj->GetVertex(vidx[i]));
		int b = draw->AddVertex(obj->GetVertex(vidx[(i + 1) % nv]));
		int seg[2] = {a, b};
		draw->AddFace(2, seg);
	}
}

//---------------------------------------------------------------------------
// persistence
//---------------------------------------------------------------------------
void SmoothingGroupPlugin::OnSaveDocument(MQDocument doc, const char *filename, SAVE_DOCUMENT_PARAM& param)
{
	if (param.elem == NULL) return;
	int uid = m_UserDataID;
	if (uid <= 0) return;

	static const char *hex = "0123456789abcdef";
	int numObj = doc->GetObjectCount();
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
			if (obj->GetFacePointCount(fi) > 0)
				obj->GetFaceUserData(uid, fi, &mask);
			if (mask) any = true;
			for (int k = 7; k >= 0; k--)
				data += hex[(mask >> (k * 4)) & 0xF];
		}
		if (!any) continue;

		MQXmlElement child = param.elem->AddChildElement("SG");
		char numbuf[16];
		sprintf(numbuf, "%d", oi);
		child->SetAttribute("obj", numbuf);
		child->SetAttribute("data", data.c_str());
	}
}

void SmoothingGroupPlugin::OnNewDocument(MQDocument doc, const char *filename, NEW_DOCUMENT_PARAM& param)
{
	m_UserDataID = -1;
	int uid = EnsureUserData(doc);
	if (uid <= 0 || param.elem == NULL) return;

	int numObj = doc->GetObjectCount();
	for (MQXmlElement child = param.elem->FirstChildElement("SG");
	     child != NULL; child = param.elem->NextChildElement("SG", child)){
		int oi = atoi(child->GetAttribute("obj").c_str());
		if (oi < 0 || oi >= numObj) continue;
		MQObject obj = doc->GetObject(oi);
		if (obj == NULL) continue;
		obj->AllocFaceUserData(uid);
		std::string data = child->GetAttribute("data");
		int nf = obj->GetFaceCount();
		for (int fi = 0; fi < nf; fi++){
			if (obj->GetFacePointCount(fi) == 0) continue;
			if ((size_t)(fi * 8 + 8) > data.size()) break;
			unsigned int mask = (unsigned int)strtoul(data.substr(fi * 8, 8).c_str(), NULL, 16);
			obj->SetFaceUserData(uid, fi, &mask);
		}
	}
}

void SmoothingGroupPlugin::OnEndDocument(MQDocument doc)
{
	m_UserDataID = -1;
}

//---------------------------------------------------------------------------
MQBasePlugin *GetPluginClass()
{
	static SmoothingGroupPlugin plugin;
	return &plugin;
}

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH){
		g_hInstance = hinstDLL;
	}
	return TRUE;
}
