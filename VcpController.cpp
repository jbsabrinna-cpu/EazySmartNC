#include "VcpController.h"
#include "VcpEvents.h"
#include <QThread>

VcpController::VcpController(QObject *parent)
    : QObject(parent)
{
}

void VcpController::setApi(CentroidBridgeWrapper *a)
{
    api = a;
}

void VcpController::momentary(int code)
{
    if (!api) return;

    api->handleVcpButtonRaw(code);
    QThread::msleep(20);
    api->handleVcpButtonRaw(-code);
}

//────────────────────────────────────────────
// Jog logic (CONT réactif + continu doux + nudge dynamique)
//────────────────────────────────────────────
void VcpController::handleJog(bool pressed, int eventCode)
{
    if (!api) return;

    if (pressed)
    {
        jogTimer.restart();

        // Jog continu immédiat
        api->handleVcpButtonRaw(eventCode);

        //────────────────────────────────────────────
        // 🔵 RAMPING CONTINU (douceur du départ)
        //────────────────────────────────────────────
        // Valeur recommandée : 20–100 ms
        // 20 ms = très réactif
        // 50 ms = doux
        // 100 ms = très doux
        QThread::msleep(100);   // ← AJUSTE ICI pour la douceur du continu
    }
    else
    {
        qint64 duration = jogTimer.elapsed();

        // STOP immédiat
        api->handleVcpButtonRaw(-eventCode);

        //────────────────────────────────────────────
        // 🔵 CLIC COURT = NUDGE DYNAMIQUE
        //────────────────────────────────────────────
        // duration < seuil → clic court
        // seuil recommandé : 100–150 ms
        if (duration < 120)   // ← AJUSTE ICI : seuil entre clic court et continu
        {
            //────────────────────────────────────────
            // 🔵 CALCUL DU NUDGE DYNAMIQUE
            //────────────────────────────────────────
            // nudge = durée du clic * facteur
            // facteur recommandé : 0.3 à 0.6
            int nudge = duration * 0.5;   // ← AJUSTE ICI : proportion du nudge

            //────────────────────────────────────────
            // 🔵 LIMITE HAUTE (distance max du clic court)
            //────────────────────────────────────────
            // 40 ms = ~0.7 mm
            // 60 ms = ~1 mm (CNC12)
            // 80 ms = ~1.3 mm
            if (nudge > 60)
                nudge = 60;   // ← AJUSTE ICI : nudge maximum

            //────────────────────────────────────────
            // 🔵 LIMITE BASSE (évite les micro-mouvements)
            //────────────────────────────────────────
            // 5 ms = très petit
            // 10 ms = recommandé
            if (nudge < 10)
                nudge = 10;   // ← AJUSTE ICI : nudge minimum

            //────────────────────────────────────────
            // 🔵 APPLICATION DU NUDGE
            //────────────────────────────────────────
            api->handleVcpButtonRaw(eventCode);
            QThread::msleep(nudge);
            api->handleVcpButtonRaw(-eventCode);
        }
    }
}

//────────────────────────────────────────────
// Jog axes
//────────────────────────────────────────────
void VcpController::jogXPlus(bool pressed)  { handleJog(pressed, VcpEvent::JOG_X_PLUS); }
void VcpController::jogXMinus(bool pressed) { handleJog(pressed, VcpEvent::JOG_X_MINUS); }
void VcpController::jogYPlus(bool pressed)  { handleJog(pressed, VcpEvent::JOG_Y_PLUS); }
void VcpController::jogYMinus(bool pressed) { handleJog(pressed, VcpEvent::JOG_Y_MINUS); }
void VcpController::jogZPlus(bool pressed)  { handleJog(pressed, VcpEvent::JOG_Z_PLUS); }
void VcpController::jogZMinus(bool pressed) { handleJog(pressed, VcpEvent::JOG_Z_MINUS); }

//────────────────────────────────────────────
// Modes JOG
//────────────────────────────────────────────
void VcpController::toggleIncCont()
{
    momentary(VcpEvent::JOG_MODE_TOGGLE);
}

void VcpController::setX1()
{
    momentary(VcpEvent::JOG_INC_1);
}

void VcpController::setX10()
{
    momentary(VcpEvent::JOG_INC_10);
}

void VcpController::setX100()
{
    momentary(VcpEvent::JOG_INC_100);
}

void VcpController::toggleRapid()
{
    momentary(VcpEvent::JOG_RAPID);
}

void VcpController::toggleMPG()
{
    momentary(VcpEvent::JOG_MPG);
}

void VcpController::toggleManAuto()
{
    momentary(VcpEvent::SPINDLE_AUTO);
}

//────────────────────────────────────────────
// Feed override
//────────────────────────────────────────────
void VcpController::feedPlus(bool pressed)
{
    if (!api) return;
    api->handleVcpButtonRaw( pressed ? VcpEvent::FEED_PLUS : -VcpEvent::FEED_PLUS );
}

void VcpController::feedMinus(bool pressed)
{
    if (!api) return;
    api->handleVcpButtonRaw( pressed ? VcpEvent::FEED_MINUS : -VcpEvent::FEED_MINUS );
}

void VcpController::feedPreset100() { momentary(VcpEvent::FEED_100); }
void VcpController::feedPreset75()  { momentary(VcpEvent::FEED_75); }
void VcpController::feedPreset50()  { momentary(VcpEvent::FEED_50); }
void VcpController::feedPreset25()  { momentary(VcpEvent::FEED_25); }
void VcpController::feedHold()      { momentary(VcpEvent::FEED_HOLD); }

//────────────────────────────────────────────
// Spindle override
//────────────────────────────────────────────
void VcpController::spindlePlus(bool pressed)
{
    if (!api) return;
    api->handleVcpButtonRaw( pressed ? VcpEvent::SPINDLE_PLUS : -VcpEvent::SPINDLE_PLUS );
}

void VcpController::spindleMinus(bool pressed)
{
    if (!api) return;
    api->handleVcpButtonRaw( pressed ? VcpEvent::SPINDLE_MINUS : -VcpEvent::SPINDLE_MINUS );
}

void VcpController::spindlePreset100()
{
    momentary(VcpEvent::SPINDLE_100);
}

//────────────────────────────────────────────
// Cycle
//────────────────────────────────────────────
void VcpController::cycleStart() { momentary(VcpEvent::CYCLE_START); }
void VcpController::cycleStop()  { momentary(VcpEvent::CYCLE_STOP); }
void VcpController::reset()      { momentary(VcpEvent::RESET); }

//────────────────────────────────────────────
// Spindle
//────────────────────────────────────────────
void VcpController::spindleCW()    { momentary(VcpEvent::SPINDLE_CW); }
void VcpController::spindleCCW()   { momentary(VcpEvent::SPINDLE_CCW); }
void VcpController::spindleStop()  { momentary(VcpEvent::SPINDLE_STOP); }
void VcpController::spindleStart() { momentary(VcpEvent::SPINDLE_START); }
