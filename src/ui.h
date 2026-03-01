#pragma once

#include <lvgl.h>
#include "data_fetcher.h"

// =============================================================================
// UI Manager - gestisce le 3 pagine LVGL con swipe orizzontale
//
// Pagina 0: Data + Messaggio
// Pagina 1: Meteo
// Pagina 2: Alert
// =============================================================================

class UIManager {
public:
    UIManager();

    // Inizializza LVGL e crea tutte le pagine
    void init();

    // Aggiorna i contenuti con i nuovi dati
    void updateData(const DisplayData& data);

    // Mostra schermata di connessione WiFi in corso
    void showConnecting();

    // Mostra errore di rete
    void showError(const String& msg);

    // Da chiamare nel loop principale - gestisce LVGL tick
    void tick();

    // Ritorna l'indice della pagina correntemente visualizzata (0-2)
    int currentPage() const { return _currentPage; }

private:
    // Contenitori principali
    lv_obj_t* _tabview;
    lv_obj_t* _tabPage1;
    lv_obj_t* _tabPage2;
    lv_obj_t* _tabPage3;
    lv_obj_t* _overlayScreen;  // Schermata overlay (connessione/errore)

    // Widgets pagina 1: Data + Messaggio
    lv_obj_t* _lblDate;
    lv_obj_t* _lblMessage;
    lv_obj_t* _lblP1Status;

    // Widgets pagina 2: Meteo
    lv_obj_t* _lblWeatherIcon;
    lv_obj_t* _lblWeatherText;
    lv_obj_t* _lblP2Status;

    // Widgets pagina 3: Alert
    lv_obj_t* _lblAlertIcon;
    lv_obj_t* _lblAlertText;
    lv_obj_t* _lblP3Status;

    // Navigazione dots
    lv_obj_t* _dotContainer;
    lv_obj_t* _dots[3];

    // Overlay
    lv_obj_t* _lblOverlayMsg;
    lv_obj_t* _spinnerOverlay;

    int _currentPage;
    bool _overlayVisible;

    void _createPage1(lv_obj_t* parent);
    void _createPage2(lv_obj_t* parent);
    void _createPage3(lv_obj_t* parent);
    void _createNavDots(lv_obj_t* parent);
    void _updateNavDots(int activePage);
    void _createOverlay();
    void _applyDarkTheme();

    // Callback cambio tab (per aggiornare i dots)
    static void _onTabChanged(lv_event_t* e);
};

// Funzioni di setup LVGL (display + touch driver)
void lvgl_display_init();
void lvgl_touch_init();
void lvgl_tick_timer_init();
