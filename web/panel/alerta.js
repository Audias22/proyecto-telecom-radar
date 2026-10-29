// Modo emergencia: título de la pestaña parpadeando y sonido mientras haya un BOTON sin atender.
// Los navegadores no dejan reproducir sonido hasta que la persona interactúa con la página, así
// que el audio se habilita con el primer toque o tecla.

const TITULO_NORMAL = "Monitor de respiración";
const TITULO_ALERTA = "AUXILIO: botón presionado";

let contexto = null;
let activa = false;
let silenciada = false;
let intervaloTitulo = null;
let intervaloSonido = null;
let alCambiarSonido = () => {};

function habilitarAudio() {
  if (contexto) return;
  const Ctx = window.AudioContext || window.webkitAudioContext;
  if (!Ctx) return;
  contexto = new Ctx();
  if (contexto.state === "suspended") contexto.resume();
  alCambiarSonido();
  if (activa && !silenciada) sonar();
}

for (const tipo of ["pointerdown", "keydown", "touchstart"]) {
  window.addEventListener(tipo, habilitarAudio, { once: false, passive: true, capture: true });
}

function tono(frecuencia, inicio, duracion) {
  const osc = contexto.createOscillator();
  const vol = contexto.createGain();
  osc.type = "square";
  osc.frequency.value = frecuencia;
  vol.gain.setValueAtTime(0.0001, inicio);
  vol.gain.exponentialRampToValueAtTime(0.18, inicio + 0.02);
  vol.gain.exponentialRampToValueAtTime(0.0001, inicio + duracion);
  osc.connect(vol).connect(contexto.destination);
  osc.start(inicio);
  osc.stop(inicio + duracion + 0.05);
}

function sonar() {
  if (!contexto || silenciada || !activa) return;
  const t = contexto.currentTime + 0.02;
  tono(880, t, 0.22);
  tono(660, t + 0.28, 0.22);
  tono(880, t + 0.56, 0.22);
}

export function estadoSonido() {
  if (silenciada) return "silenciado";
  return contexto ? "activo" : "pendiente";
}

export function alCambiar(fn) {
  alCambiarSonido = fn;
}

export function silenciar(valor) {
  silenciada = valor;
  alCambiarSonido();
}

export function activarEmergencia(valor) {
  if (valor === activa) return;
  activa = valor;
  clearInterval(intervaloTitulo);
  clearInterval(intervaloSonido);
  if (activa) {
    silenciada = false;
    let alterna = false;
    document.title = TITULO_ALERTA;
    intervaloTitulo = setInterval(() => {
      alterna = !alterna;
      document.title = alterna ? `(!) ${TITULO_NORMAL}` : TITULO_ALERTA;
    }, 1000);
    sonar();
    intervaloSonido = setInterval(sonar, 2500);
  } else {
    document.title = TITULO_NORMAL;
  }
  alCambiarSonido();
}
