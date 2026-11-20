#pragma once
// ═══════════════════════════════════════════════════════════════
//  progression.h — avancement du calcul du G-code (lu par la fenêtre
//  « Calcul du G-code », écrit par buildGCode et les générateurs,
//  qui tournent dans un autre fil). Atomiques : lecture sans verrou.
// ═══════════════════════════════════════════════════════════════
#include <atomic>

namespace Progression {
inline std::atomic<int>  bloc{0};        // bloc en cours (1..nbBlocs)
inline std::atomic<int>  nbBlocs{0};
inline std::atomic<int>  typeBloc{-1};   // BlockType du bloc en cours
inline std::atomic<int>  phase{0};       // 0 -, 1 ébauche, 2 finition, 3 balayage
inline std::atomic<int>  etape{0};       // couche / ligne en cours
inline std::atomic<int>  nbEtapes{0};
inline std::atomic<bool> annuler{false};
inline std::atomic<bool> enCours{false};   // calcul du G-code lancé (les vues 3D attendent)
inline void raz() { bloc = 0; nbBlocs = 0; typeBloc = -1; phase = 0; etape = 0; nbEtapes = 0; annuler = false; }
}
