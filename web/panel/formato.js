// Formatos y criterios compartidos del panel.

export const MINUTO = 60 * 1000;
export const HORA = 60 * MINUTO;

// docs/modelo-datos.md, sección 3: la base está desconectada si ultimo_contacto tiene más de 3 min.
export const BASE_DESCONECTADA_MS = 3 * MINUTO;
// docs/protocolo.md, COLGANTE_AUSENTE_S = 2700 s.
export const COLGANTE_AUSENTE_MS = 45 * MINUTO;
// docs/protocolo.md, BATERIA_BAJA_MV.
export const BATERIA_BAJA_MV = 3500;
// La base escribe una lectura cada 5 s; sin lecturas en 30 s el radar se considera sin datos.
export const RADAR_SIN_DATOS_MS = 30 * 1000;
// docs/radar.md, 13.6: umbral de actividad para "persona detectada".
export const UMBRAL_ACTIVIDAD = 0.03;

export const TIPOS_EVENTO = {
  BOTON: "Botón de auxilio",
  BATERIA_BAJA: "Batería baja del colgante",
  COLGANTE_AUSENTE: "Colgante sin señal",
  RADAR_SIN_DATOS: "Radar sin datos",
  BASE_DESCONECTADA: "Base desconectada",
  REINICIO: "Reinicio de la base",
};

export function nombreEvento(tipo) {
  return TIPOS_EVENTO[tipo] || tipo || "Evento";
}

const fmtHora = new Intl.DateTimeFormat("es-GT", { hour: "2-digit", minute: "2-digit", hour12: false });
const fmtHoraSeg = new Intl.DateTimeFormat("es-GT", { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false });
const fmtFechaCorta = new Intl.DateTimeFormat("es-GT", { day: "numeric", month: "short" });
const fmtFechaLarga = new Intl.DateTimeFormat("es-GT", { weekday: "long", day: "numeric", month: "long" });

export const hora = (ms) => fmtHora.format(ms);
export const horaSeg = (ms) => fmtHoraSeg.format(ms);
export const fechaCorta = (ms) => fmtFechaCorta.format(ms);
export const fechaLarga = (ms) => fmtFechaLarga.format(ms);

export function mismoDia(a, b) {
  const x = new Date(a);
  const y = new Date(b);
  return x.getFullYear() === y.getFullYear() && x.getMonth() === y.getMonth() && x.getDate() === y.getDate();
}

// "hace 12 s", "hace 3 min", "hace 2 h 5 min", "hace 3 días"
export function hace(ms, ahora) {
  const d = Math.max(0, ahora - ms);
  if (d < 5000) return "ahora";
  if (d < MINUTO) return `hace ${Math.floor(d / 1000)} s`;
  if (d < HORA) return `hace ${Math.floor(d / MINUTO)} min`;
  if (d < 24 * HORA) {
    const h = Math.floor(d / HORA);
    const m = Math.floor((d % HORA) / MINUTO);
    return m ? `hace ${h} h ${m} min` : `hace ${h} h`;
  }
  const dias = Math.floor(d / (24 * HORA));
  return dias === 1 ? "hace 1 día" : `hace ${dias} días`;
}

// Porcentaje aproximado de una celda Li-ion en reposo a partir del voltaje.
// Curva típica; docs/protocolo.md dice que el umbral se confirmará con una curva medida.
const CURVA_LIION = [
  [3300, 0], [3400, 3], [3500, 8], [3600, 15], [3700, 30],
  [3800, 50], [3900, 65], [4000, 80], [4100, 91], [4200, 100],
];

export function porcentajeBateria(mv) {
  if (!mv) return null;
  if (mv <= CURVA_LIION[0][0]) return 0;
  for (let i = 1; i < CURVA_LIION.length; i++) {
    const [v1, p1] = CURVA_LIION[i];
    if (mv <= v1) {
      const [v0, p0] = CURVA_LIION[i - 1];
      return Math.round(p0 + ((mv - v0) / (v1 - v0)) * (p1 - p0));
    }
  }
  return 100;
}

// Barras de señal (0-4) a partir del RSSI en dBm.
export function barrasRssi(rssi) {
  if (typeof rssi !== "number") return 0;
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  if (rssi >= -90) return 1;
  return 0;
}

export function calidadRssi(rssi) {
  return ["sin señal", "débil", "regular", "buena", "excelente"][barrasRssi(rssi)];
}

export function numero(v, decimales = 0) {
  return Number(v).toLocaleString("es-GT", { minimumFractionDigits: decimales, maximumFractionDigits: decimales });
}

export function escapar(texto) {
  return String(texto ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
}
