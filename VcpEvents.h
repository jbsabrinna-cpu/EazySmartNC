#pragma once

// ============================================================
// VcpEvents.h — Codes SkinEvent CNC12 v5.4
// ============================================================
namespace VcpEvent
{
// Jog axes
constexpr int JOG_X_MINUS    = 37;
constexpr int JOG_X_PLUS     = 39;
constexpr int JOG_Y_MINUS    = 43;
constexpr int JOG_Y_PLUS     = 33;
constexpr int JOG_Z_MINUS    = 45;
constexpr int JOG_Z_PLUS     = 35;

// Mode jog
constexpr int JOG_MODE_TOGGLE = 26;
constexpr int JOG_INC_1       = 27;
constexpr int JOG_INC_10      = 28;
constexpr int JOG_INC_100     = 29;
constexpr int JOG_MPG         = 30;
constexpr int JOG_RAPID       = 38;

// Cycle
constexpr int CYCLE_START    = 50;
constexpr int CYCLE_STOP     = 46;
constexpr int FEED_HOLD      = 49;
constexpr int RESET          = 56;

// Spindle
constexpr int SPINDLE_START  = 17;
constexpr int SPINDLE_STOP   = 16;
constexpr int SPINDLE_CW     = 7;
constexpr int SPINDLE_CCW    = 12;
constexpr int SPINDLE_AUTO   = 2;
constexpr int SPINDLE_PLUS   = 1;
constexpr int SPINDLE_MINUS  = 11;
constexpr int SPINDLE_100    = 6;

// Feed override
constexpr int FEED_PLUS      = 54;
constexpr int FEED_MINUS     = 52;
constexpr int FEED_100       = 53;
constexpr int FEED_75        = 111;
constexpr int FEED_50        = 112;
constexpr int FEED_25        = 113;
}
