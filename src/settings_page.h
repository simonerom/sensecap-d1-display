// =============================================================================
// settings_page.h — Settings form widget
//
// Builds the settings LVGL widget tree (WiFi, server, timezone, save button).
// Also owns the fullscreen touch calibration overlay.
// Called by WidgetFactory::buildSettingsForm() when rendering <settings_form/>.
// =============================================================================
#pragma once

#include <lvgl.h>
#include <functional>
#include "settings_manager.h"
#include "lv_helpers.h"
#include "../include/config.h"

// Number of calibration touch points
#define CAL_POINT_COUNT 5

class SettingsPage {
public:
    SettingsPage();

    // Store callbacks (call before build).
    void setCallbacks(std::function<void(const AppSettings&)> onSave,
                      std::function<void(const TouchCalibration&)> onCalDone);

    // Build all widgets into parent. Must be called after setCallbacks().
    void build(lv_obj_t* parent,
               std::function<void(const AppSettings&)> onSave = nullptr,
               std::function<void(const TouchCalibration&)> onCalDone = nullptr);

    // Pre-fill fields with current NVS values.
    void populate(const AppSettings& s);

    // Apply calibration (updates global touch coefficients).
    void applyCalibration(const TouchCalibration& cal);

    // Open the fullscreen calibration screen (called internally by Calibrate button,
    // but also callable from ScreenManager on external request).
    void startCalibration();

private:
    // ---- Settings fields ----
    lv_obj_t* _taSSID;
    lv_obj_t* _taPassword;
    lv_obj_t* _taHost;
    lv_obj_t* _taPort;
    lv_obj_t* _spinboxTZ;
    lv_obj_t* _btnSave;
    lv_obj_t* _btnShowPwd;

    // ---- Fullscreen keyboard panel ----
    lv_obj_t* _kbdPanel;
    lv_obj_t* _kbdFieldLabel;
    lv_obj_t* _kbdPreview;
    lv_obj_t* _keyboard;
    lv_obj_t* _activeTA;

    // ---- Calibration overlay ----
    lv_obj_t* _calScreen;
    lv_obj_t* _calCrosshair;
    lv_obj_t* _calInstruction;
    lv_obj_t* _calProgress;
    lv_obj_t* _calBtnSkip;
    int       _calStep;
    int16_t   _calRawX[CAL_POINT_COUNT];
    int16_t   _calRawY[CAL_POINT_COUNT];

    // ---- Callbacks ----
    std::function<void(const AppSettings&)>      _onSave;
    std::function<void(const TouchCalibration&)> _onCalDone;

    // ---- Internal builders ----
    void _buildKeyboardPanel(lv_obj_t* screenParent);
    void _buildCalibrationScreen(lv_obj_t* screenParent);

    // ---- Keyboard helpers ----
    void _showKeyboard(lv_obj_t* ta);
    void _hideKeyboard();

    // ---- Calibration helpers ----
    void _advanceCalStep();
    void _finishCalibration();

    // ---- Static callbacks ----
    static void _onTAFocused(lv_event_t* e);
    static void _onKeyboardReady(lv_event_t* e);
    static void _onSaveClicked(lv_event_t* e);
    static void _onSpinboxInc(lv_event_t* e);
    static void _onSpinboxDec(lv_event_t* e);
    static void _onShowPwdClicked(lv_event_t* e);
    static void _onCalibrateClicked(lv_event_t* e);
    static void _onCalTap(lv_event_t* e);
    static void _onCalSkip(lv_event_t* e);
};
