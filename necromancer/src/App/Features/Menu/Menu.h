#pragma once

#include "../../../SDK/SDK.h"
#include "MenuAnimations.h"
#include <unordered_map>

// Draggable GroupBox system
enum class EGroupBoxColumn { LEFT = 0, MIDDLE = 1, RIGHT = 2 };
enum class EGroupBoxSize { EXTRA_SMALL = 0, SMALL = 1, MEDIUM = 2, BIG = 3, VERY_BIG = 4 };

struct DraggableGroupBox_t
{
	std::string m_szId;           // Unique identifier (tab_name)
	std::string m_szLabel;        // Display label
	EGroupBoxColumn m_nColumn;    // Current column (0=left, 1=middle, 2=right)
	int m_nOrderInColumn;         // Order within the column
	int m_nWidth;                 // GroupBox width
	int m_nHeight;                // Calculated height after rendering
	int m_nRenderX;               // Last rendered X position
	int m_nRenderY;               // Last rendered Y position
	EGroupBoxSize m_eSize;        // Size category for column placement rules
	std::function<void()> m_fnRenderContent; // Function to render the content
};

class CMenu
{
private:
	bool m_bOpen = false;
	bool m_bMenuWindowHovered = false;
	int m_nCursorX = 0, m_nCursorY = 0;

	int m_nLastGroupBoxY = 0, m_nLastGroupBoxW = 0;
	int m_nLastButtonW = 0;

	bool m_bClickConsumed = false;
	std::map<void *, bool> m_mapStates = {};
	
	// Animation system
	CAnimationController m_animator;
	CParticleSystem m_particles;
	std::map<std::string, float> m_buttonHoverStates;
	std::map<std::string, float> m_buttonPressStates;
	float m_flLastFrameTime = 0.0f;
	float m_flMenuOpenProgress = 0.0f;
	bool m_bWasOpen = false;

	// Autosave system
	bool m_bConfigChanged = false;
	float m_flLastChangeTime = 0.0f;
	static constexpr float AUTOSAVE_DELAY = 2.0f; // Save 2 seconds after last change

	// Draggable GroupBox system
	std::map<std::string, DraggableGroupBox_t> m_mapGroupBoxes;
	std::string m_strDraggingGroupBox;      // ID of currently dragged GroupBox
	bool m_bIsDraggingGroupBox = false;
	int m_nDragOffsetX = 0, m_nDragOffsetY = 0;
	EGroupBoxColumn m_nHoveredDropColumn = EGroupBoxColumn::LEFT;
	bool m_bShowDropZones = false;
	float m_flDropZoneAlpha = 0.0f;
	std::map<std::string, float> m_mapGroupBoxDragAlpha; // For smooth drag animation
	int m_nDragContentY = 0;  // Content Y position for drop calculations

	//std::string m_strConfigPath = {};

	std::unique_ptr<Color_t[]> m_pGradient = nullptr;
	unsigned int m_nColorPickerTextureId = 0;

private:
	void Drag(int &x, int &y, int w, int h, int offset_y);
	bool IsHovered(int x, int y, int w, int h, void *pVar, bool bStrict = false);
	bool IsHoveredSimple(int x, int y, int w, int h);

	bool CheckBox(const char *szLabel, bool &bVar);
	bool SliderFloat(const char *szLabel, float &flVar, float flMin, float flMax, float flStep, const char *szFormat);
	bool SliderInt(const char *szLabel, int &nVar, int nMin, int nMax, int nStep);
	bool InputKey(const char *szLabel, int &nKeyOut);
	bool Button(const char *szLabel, bool bActive = false, int nCustomWidth = 0);
	bool playerListButton(const wchar_t *label, int nCustomWidth, Color_t clr, bool center_txt);
	bool InputText(const char *szLabel, const char *szLabel2, std::string &strOutput);
	bool SelectSingle(const char *szLabel, int &nVar, const std::vector<std::pair<const char *, int>> &vecSelects);
	bool SelectMulti(const char *szLabel, std::vector<std::pair<const char *, bool &>> &vecSelects);
	bool ColorPicker(const char *szLabel, Color_t &colVar);
	void GroupBoxStart(const char *szLabel, int nWidth);
	void GroupBoxEnd();

	// Draggable GroupBox methods
	void RegisterGroupBox(const std::string& szTab, const std::string& szLabel, EGroupBoxColumn nDefaultColumn, int nOrder, int nWidth, EGroupBoxSize eSize = EGroupBoxSize::SMALL);
	void RenderDraggableGroupBoxes(const std::string& szTab, int nContentX, int nContentY, int nContentW, int nContentH);
	void RenderDropZones(int nContentX, int nContentY, int nContentW, int nContentH);
	void HandleGroupBoxDrag();
	EGroupBoxColumn GetColumnFromMouseX(int nContentX, int nContentW);
	void ReorderGroupBoxesInColumn(const std::string& szTab, EGroupBoxColumn nColumn);
	std::string GetGroupBoxConfigKey(const std::string& szId);

public:
	inline bool IsOpen() { return m_bOpen; }
	inline bool IsMenuWindowHovered() { return m_bMenuWindowHovered; }
	inline bool IsDraggingGroupBox() { return m_bIsDraggingGroupBox; }

	bool m_bWantTextInput = false;
	bool m_bInKeybind = false;
	
	// Mark config as changed (triggers autosave after delay)
	void MarkConfigChanged();

private:
	void ProcessAutosave();
	void MainWindow();
	void Snow();
	void Indicators();

public:
	void Run();
	CMenu();

private:
	std::unordered_map<Color_t*, bool> m_mapContextMenuStates;
	Color_t m_copiedColor = { 255, 255, 255, 255 };

	std::unordered_map<uint32, bool> m_mapPlayerContextMenuStates;
};

MAKE_SINGLETON_SCOPED(CMenu, Menu, F);