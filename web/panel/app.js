// Panel del cuidador: sesión, listeners de Realtime Database y vista.
// Modelo de datos en docs/modelo-datos.md; documentación del panel en docs/panel.md.

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.14.1/firebase-app.js";
import {
  getAuth, onAuthStateChanged, signInWithEmailAndPassword, signOut,
} from "https://www.gstatic.com/firebasejs/10.14.1/firebase-auth.js";
import {
  getDatabase, ref, query, orderByKey, orderByChild, startAt, limitToLast, equalTo,
  onValue, onChildAdded, onChildChanged, onChildRemoved, update, serverTimestamp,
} from "https://www.gstatic.com/firebasejs/10.14.1/firebase-database.js";

import { firebaseConfig, BASE_ID } from "./config.js";
import {
  MINUTO, HORA, BASE_DESCONECTADA_MS, COLGANTE_AUSENTE_MS, BATERIA_BAJA_MV, RADAR_SIN_DATOS_MS,
  UMBRAL_ACTIVIDAD, hace, hora, horaSeg, fechaCorta, fechaLarga, mismoDia, nombreEvento,
  porcentajeBateria, barrasRssi, calidadRssi, numero, escapar,
} from "./formato.js";
import { dibujarGrafica, dibujarCinta, ocultarGlobo, buscarDesde } from "./graficas.js";
import { activarEmergencia, silenciar, estadoSonido, alCambiar } from "./alerta.js";
import { iniciarAmpliada, abrirAmpliada, redibujar as redibujarAmpliada, estaAbierta } from "./ampliada.js";

const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db = getDatabase(app);

// Ventana que se descarga: 6 h (la cinta). Las gráficas muestran una parte de ella.
const VENTANA_MS = 6 * HORA;
const EVENTOS_RECIENTES = 80;
const reducirMovimiento = window.matchMedia("(prefers-reduced-motion: reduce)");

const $ = (id) => document.getElementById(id);

const estado = {
  usuario: null,
  offset: 0,
  conectado: true,
  base: null,
  colgantes: {},
  lecturas: [],
  recientes: {},
  pendientes: {},
  errores: new Map(),
  rangoMin: Number(leerPreferencia("rango", 60)) || 60,
  guardando: new Set(),
  erroresAtender: new Map(),
  primeraCargaEventos: true,
  eventosVistos: new Set(),
};

let desuscribir = [];
let dibujoPendiente = false;

const ahora = () => Date.now() + estado.offset;

function leerPreferencia(clave, porDefecto) {
  try {
    return localStorage.getItem(`panel.${clave}`) ?? porDefecto;
  } catch {
    return porDefecto;
  }
}

function guardarPreferencia(clave, valor) {
  try {
    localStorage.setItem(`panel.${clave}`, valor);
  } catch { /* sin almacenamiento local: no pasa nada */ }
}

// ---------------------------------------------------------------------------
// Sesión
// ---------------------------------------------------------------------------

const MENSAJES_AUTH = {
  "auth/invalid-credential": "Correo o contraseña incorrectos.",
  "auth/invalid-login-credentials": "Correo o contraseña incorrectos.",
  "auth/wrong-password": "Correo o contraseña incorrectos.",
  "auth/user-not-found": "Correo o contraseña incorrectos.",
  "auth/invalid-email": "El correo no tiene un formato válido.",
  "auth/user-disabled": "Esta cuenta está desactivada.",
  "auth/too-many-requests": "Demasiados intentos. Espera unos minutos y vuelve a probar.",
  "auth/network-request-failed": "Sin conexión a internet. Revisa la red y vuelve a probar.",
};

$("form-sesion").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const correo = $("sesion-correo").value.trim();
  const clave = $("sesion-clave").value;
  const error = $("sesion-error");
  if (!correo || !clave) {
    error.textContent = "Escribe el correo y la contraseña.";
    return;
  }
  error.textContent = "";
  $("sesion-entrar").disabled = true;
  $("sesion-entrar").textContent = "Entrando…";
  try {
    await signInWithEmailAndPassword(auth, correo, clave);
    $("sesion-clave").value = "";
  } catch (e) {
    error.textContent = MENSAJES_AUTH[e.code] || `No se pudo iniciar sesión (${e.code || e.message}).`;
  } finally {
    $("sesion-entrar").disabled = false;
    $("sesion-entrar").textContent = "Entrar";
  }
});

$("cerrar-sesion").addEventListener("click", () => signOut(auth));

onAuthStateChanged(auth, (usuario) => {
  document.body.classList.remove("cargando");
  detenerListeners();
  estado.usuario = usuario;
  if (usuario) {
    $("vista-sesion").hidden = true;
    $("vista-panel").hidden = false;
    $("enc-correo").textContent = usuario.email || "";
    $("enc-base").textContent = BASE_ID;
    iniciarListeners();
  } else {
    $("vista-panel").hidden = true;
    $("vista-sesion").hidden = false;
    reiniciarEstado();
    activarEmergencia(false);
    actualizarEmergencia();
  }
  pintarAvisos();
});

// ---------------------------------------------------------------------------
// Listeners
// ---------------------------------------------------------------------------

function reiniciarEstado() {
  Object.assign(estado, {
    base: null, colgantes: {}, lecturas: [], recientes: {}, pendientes: {},
    primeraCargaEventos: true,
  });
  estado.errores.clear();
  estado.eventosVistos.clear();
}

function detenerListeners() {
  for (const fn of desuscribir) fn();
  desuscribir = [];
}

function alFallar(clave) {
  return (e) => {
    console.error(`listener ${clave}:`, e.code || "", e.message);
    estado.errores.set(clave, e);
    pintarAvisos();
  };
}

function limpiarError(clave) {
  if (estado.errores.delete(clave)) pintarAvisos();
}

function iniciarListeners() {
  reiniciarEstado();

  desuscribir.push(onValue(ref(db, ".info/connected"), (s) => {
    estado.conectado = s.val() === true;
    pintarAvisos();
  }));

  desuscribir.push(onValue(ref(db, ".info/serverTimeOffset"), (s) => {
    estado.offset = Math.round(s.val() || 0);
  }));

  // Las lecturas se piden después de la primera respuesta autorizada (el estado de la base): si
  // la consulta sale antes de que la conexión tenga el token de la sesión, Firebase la rechaza
  // con permission_denied y la cancela.
  let lecturasIniciadas = false;
  desuscribir.push(onValue(ref(db, `dispositivos/${BASE_ID}/estado`), (s) => {
    if (!lecturasIniciadas) {
      lecturasIniciadas = true;
      escucharLecturas();
    }
    limpiarError("base");
    estado.base = s.val();
    pintarBase(true);
  }, alFallar("base")));

  desuscribir.push(onValue(ref(db, "colgantes"), (s) => {
    limpiarError("colgantes");
    estado.colgantes = s.val() || {};
    pintarColgantes(true);
  }, alFallar("colgantes")));

  // Últimos eventos para la lista y, aparte, todos los que siguen sin atender (aunque sean
  // viejos), para que el modo emergencia no dependa de cuántos eventos haya.
  desuscribir.push(onValue(query(ref(db, "eventos"), orderByChild("timestamp"), limitToLast(EVENTOS_RECIENTES)), (s) => {
    limpiarError("eventos");
    estado.recientes = s.val() || {};
    alCambiarEventos();
  }, alFallar("eventos")));

  desuscribir.push(onValue(query(ref(db, "eventos"), orderByChild("atendido"), equalTo(false)), (s) => {
    limpiarError("pendientes");
    estado.pendientes = s.val() || {};
    alCambiarEventos();
  }, alFallar("pendientes")));
}

// Lecturas de las últimas 6 h por rango de claves (ts de 13 dígitos = orden por clave) y luego
// solo las nuevas. Nunca se descarga todo /lecturas.
function escucharLecturas() {
  const q = query(ref(db, `lecturas/${BASE_ID}`), orderByKey(), startAt(String(Math.round(ahora() - VENTANA_MS))));
  const guardar = (s) => {
    const l = s.val();
    if (!l || typeof l !== "object") return;
    const ts = Number(s.key);
    const lectura = { ts, ...l };
    const lista = estado.lecturas;
    if (!lista.length || lista[lista.length - 1].ts < ts) {
      lista.push(lectura);
    } else {
      const i = buscarDesde(lista, ts);
      if (lista[i] && lista[i].ts === ts) lista[i] = lectura;
      else lista.splice(i, 0, lectura); // lectura diferida (cola offline de la base)
    }
    limpiarError("lecturas");
    programarDibujo();
  };
  desuscribir.push(onChildAdded(q, guardar, alFallar("lecturas")));
  desuscribir.push(onChildChanged(q, guardar));
  desuscribir.push(onChildRemoved(q, (s) => {
    const ts = Number(s.key);
    estado.lecturas = estado.lecturas.filter((l) => l.ts !== ts);
    programarDibujo();
  }));
}

function podarLecturas() {
  const limite = ahora() - VENTANA_MS - MINUTO;
  const i = buscarDesde(estado.lecturas, limite);
  if (i > 0) estado.lecturas.splice(0, i);
}

// ---------------------------------------------------------------------------
// Avisos (sin conexión, sin permisos)
// ---------------------------------------------------------------------------

function pintarAvisos() {
  const avisos = [];
  if (estado.usuario) {
    const denegado = [...estado.errores.values()].some((e) => /permission/i.test(e.code || e.message || ""));
    if (denegado) {
      avisos.push(["error", "Sin permisos",
        `La cuenta ${escapar(estado.usuario.email)} no tiene acceso al panel. Pide que la agreguen en /usuarios_panel o cierra sesión y entra con otra cuenta.`]);
    } else if (estado.errores.size) {
      const e = [...estado.errores.values()][0];
      avisos.push(["error", "Error", `No se pudieron leer los datos: ${escapar(e.message || e.code)}`]);
    }
    if (!estado.conectado || !navigator.onLine) {
      avisos.push(["atencion", "Sin conexión",
        "No hay conexión con Firebase. Lo que ves puede estar desactualizado; el panel se pone al día solo al volver la red."]);
    }
  }
  $("avisos").innerHTML = avisos
    .map(([tono, titulo, texto]) => `<p class="aviso aviso--${tono}"><strong>${titulo}</strong><span>${texto}</span></p>`)
    .join("");
}

window.addEventListener("online", pintarAvisos);
window.addEventListener("offline", pintarAvisos);

// ---------------------------------------------------------------------------
// Utilidades de vista
// ---------------------------------------------------------------------------

// Cambia el texto de un elemento; si cambió, deja un destello de tinta.
function poner(el, texto, destello = true) {
  if (el.textContent === texto) return;
  el.textContent = texto;
  if (destello && !reducirMovimiento.matches) {
    el.classList.remove("nuevo");
    void el.offsetWidth;
    el.classList.add("nuevo");
  }
}

// Número que se anima del valor anterior al nuevo.
function animarNumero(el, valor, decimales = 0, sufijo = "") {
  const final = `${numero(valor, decimales)}${sufijo}`;
  const previo = el._valor;
  el._valor = valor;
  cancelAnimationFrame(el._animacion);
  if (typeof previo !== "number" || previo === valor || reducirMovimiento.matches) {
    poner(el, final);
    return;
  }
  const inicio = performance.now();
  const duracion = 600;
  const paso = (t) => {
    const k = Math.min(1, (t - inicio) / duracion);
    const e = 1 - (1 - k) ** 3;
    el.textContent = `${numero(previo + (valor - previo) * e, decimales)}${sufijo}`;
    if (k < 1) el._animacion = requestAnimationFrame(paso);
  };
  el._animacion = requestAnimationFrame(paso);
}

function barrasHtml(rssi) {
  const n = barrasRssi(rssi);
  return [1, 2, 3, 4].map((i) => `<i class="${i <= n ? "encendida" : ""}"></i>`).join("");
}

function sello(el, texto, tono) {
  el.textContent = texto;
  el.className = `sello sello--${tono}`;
}

// ---------------------------------------------------------------------------
// Base
// ---------------------------------------------------------------------------

function baseEnLinea() {
  const b = estado.base;
  return !!(b && typeof b.ultimo_contacto === "number" && ahora() - b.ultimo_contacto <= BASE_DESCONECTADA_MS);
}

// Por qué lo que se muestra puede no estar al día: null si la base y el panel están conectados.
function fuenteObsoleta() {
  if (!estado.conectado) return "panel sin conexión";
  if (!baseEnLinea()) return "base desconectada";
  return null;
}

function pintarBase(nuevo = false) {
  const b = estado.base;
  const art = $("equipo-base");
  if (!b) {
    sello($("base-sello"), "sin datos", "atencion");
    poner($("base-hace"), "nunca", false);
    return;
  }
  const t = ahora();
  // Sin conexión del panel no se sabe si la base sigue escribiendo: no se afirma nada.
  if (!estado.conectado) sello($("base-sello"), "sin datos (panel sin conexión)", "gris");
  else if (baseEnLinea()) sello($("base-sello"), "en línea", "bien");
  else sello($("base-sello"), "desconectada", "mal");
  const obsoleto = fuenteObsoleta() !== null;
  art.classList.toggle("obsoleto", obsoleto);
  $("base-hace").textContent = typeof b.ultimo_contacto === "number"
    ? `${hace(b.ultimo_contacto, t)} · ${hora(b.ultimo_contacto)}` : "—";
  const rssi = typeof b.rssi_wifi === "number" ? `${b.rssi_wifi} dBm · ${calidadRssi(b.rssi_wifi)}` : "sin dato";
  poner($("base-rssi"), obsoleto && typeof b.rssi_wifi === "number" ? `${b.rssi_wifi} dBm · último dato` : rssi, nuevo);
  if (nuevo) {
    $("base-barras").innerHTML = barrasHtml(b.rssi_wifi);
    poner($("base-version"), b.version_firmware || "—");
    $("base-hace").classList.remove("nuevo");
    if (!reducirMovimiento.matches) {
      void $("base-hace").offsetWidth;
      $("base-hace").classList.add("nuevo");
    }
  }
}

// ---------------------------------------------------------------------------
// Colgantes
// ---------------------------------------------------------------------------

function pilaSvg(pct, clase) {
  const lleno = pct === null ? 0 : Math.max(2, (pct / 100) * 70);
  return `<svg class="pila ${clase}" viewBox="0 0 92 44" aria-hidden="true">
    <rect class="pila__cuerpo" x="2" y="4" width="78" height="36" rx="5"/>
    <rect class="pila__polo" x="81" y="15" width="8" height="14" rx="2"/>
    <rect class="pila__carga" x="6" y="8" height="28" rx="2" width="${lleno.toFixed(1)}"/>
  </svg>`;
}

function pintarColgantes(nuevo = false) {
  const cont = $("colgantes");
  const ids = Object.keys(estado.colgantes).sort((a, b) => Number(a) - Number(b));
  if (!ids.length) {
    cont.innerHTML = `<article class="equipo"><header class="equipo__cabeza"><h3>Colgante</h3>
      <span class="sello sello--atencion">sin datos</span></header>
      <p class="radar__motivo">La base todavía no ha recibido ningún mensaje de un colgante.</p></article>`;
    return;
  }
  const t = ahora();
  for (const id of ids) {
    const c = estado.colgantes[id] || {};
    let art = cont.querySelector(`[data-colgante="${id}"]`);
    if (!art) {
      cont.querySelector(".equipo:not([data-colgante])")?.remove();
      art = document.createElement("article");
      art.className = "equipo";
      art.dataset.colgante = id;
      art.innerHTML = `<header class="equipo__cabeza"><h3>Colgante ${escapar(id)}</h3><span class="sello"></span></header>
        <div class="colgante__cuerpo">
          <div class="colgante__pila"></div>
          <div class="bateria__cifras"><div class="bateria__pct"></div><div class="bateria__mv"></div></div>
        </div>
        <dl class="ficha">
          <div><dt>Último contacto</dt><dd class="c-hace">—</dd></div>
          <div><dt>Señal en la base</dt><dd class="con-icono"><span class="barras"></span><span class="c-rssi">—</span></dd></div>
        </dl>`;
      cont.appendChild(art);
    }
    const mv = typeof c.bateria_mv === "number" ? c.bateria_mv : null;
    const pct = porcentajeBateria(mv);
    const baja = mv === 0 || (mv !== null && mv < BATERIA_BAJA_MV);
    const ausente = typeof c.ultimo_heartbeat !== "number" || t - c.ultimo_heartbeat > COLGANTE_AUSENTE_MS;
    // El colgante solo llega a Firebase a través de la base: si la base (o el panel) no está al
    // día, su último dato no dice nada del estado actual.
    const motivo = fuenteObsoleta();

    const s = art.querySelector(".sello");
    if (motivo) sello(s, `sin datos (${motivo})`, "gris");
    else if (ausente) sello(s, "ausente", "mal");
    else if (baja) sello(s, "batería baja", "atencion");
    else sello(s, "normal", "bien");
    art.classList.toggle("obsoleto", motivo !== null);
    if (mv) {
      const ultimo = typeof c.ultimo_heartbeat === "number" ? ` · último dato ${hora(c.ultimo_heartbeat)}` : "";
      poner(art.querySelector(".bateria__mv"),
        motivo ? `${numero(mv)} mV${ultimo}` : `${numero(mv)} mV · porcentaje aprox.`, nuevo);
    }

    art.querySelector(".c-hace").textContent = typeof c.ultimo_heartbeat === "number"
      ? `${hace(c.ultimo_heartbeat, t)} · ${hora(c.ultimo_heartbeat)}` : "—";

    if (nuevo) {
      const claseP = mv === 0 || mv === null ? "pila--sin" : baja ? "pila--baja" : "";
      const pila = art.querySelector(".colgante__pila");
      if (!pila.firstChild) pila.innerHTML = pilaSvg(0, claseP);
      const svg = pila.querySelector("svg");
      svg.setAttribute("class", `pila ${claseP}`);
      // La carga se anima por transición de ancho (CSS).
      requestAnimationFrame(() => {
        svg.querySelector(".pila__carga").setAttribute("width",
          (pct === null ? 0 : Math.max(2, (pct / 100) * 70)).toFixed(1));
      });
      svg.setAttribute("aria-label", pct === null ? "batería sin dato" : `batería ${pct} %`);

      const pctEl = art.querySelector(".bateria__pct");
      pctEl.classList.toggle("bateria__pct--baja", baja);
      if (pct === null) {
        pctEl._valor = undefined;
        poner(pctEl, "sin dato");
        poner(art.querySelector(".bateria__mv"), "el colgante no pudo medir la batería");
      } else {
        animarNumero(pctEl, pct, 0, " %");
      }
      art.querySelector(".barras").innerHTML = barrasHtml(c.rssi);
    }
    poner(art.querySelector(".c-rssi"), typeof c.rssi !== "number" ? "sin dato"
      : motivo ? `${c.rssi} dBm · último dato` : `${c.rssi} dBm · ${calidadRssi(c.rssi)}`, nuevo);
  }
  for (const art of cont.querySelectorAll("[data-colgante]")) {
    if (!(art.dataset.colgante in estado.colgantes)) art.remove();
  }
}

// ---------------------------------------------------------------------------
// Radar y anillo
// ---------------------------------------------------------------------------

function ultimoValor(campo, desde) {
  const l = estado.lecturas;
  for (let i = l.length - 1; i >= 0 && l[i].ts >= desde; i--) {
    if (typeof l[i][campo] === "number") return l[i][campo];
  }
  return null;
}

function pintarRadar() {
  const t = ahora();
  const ultima = estado.lecturas[estado.lecturas.length - 1];
  const reciente = ultima && t - ultima.ts <= RADAR_SIN_DATOS_MS;
  const anillo = $("anillo");
  const est = $("radar-estado");
  const motivo = $("radar-motivo");

  // La estimación del módulo llega en menos de la mitad de las lecturas; se usa la última de 30 s.
  const rpm = reciente && ultima.persona ? ultimoValor("resp_rpm", t - 30 * 1000) : null;
  const lpm = reciente ? ultimoValor("latido_bpm", t - 30 * 1000) : null;

  const fuente = fuenteObsoleta();
  if (!ultima || !reciente) {
    anillo.dataset.estado = "quieto";
    poner(est, fuente ? `Sin datos (${fuente})` : "Radar sin datos recientes", false);
    est.dataset.tono = fuente ? "apagado" : "falla";
    if (fuente === "panel sin conexión") motivo.textContent = "Este equipo perdió la conexión con Firebase; no llegan lecturas.";
    else if (fuente) motivo.textContent = `La base no se comunica; la última lectura es de las ${ultima ? hora(ultima.ts) : "—"}.`;
    else if (ultima) motivo.textContent = `La última lectura llegó ${hace(ultima.ts, t)}. Revisa la conexión del radar.`;
    else motivo.textContent = "No hay lecturas en las últimas 6 horas.";
  } else if (ultima.persona) {
    poner(est, "Persona detectada", false);
    est.dataset.tono = "";
    motivo.textContent = rpm === null
      ? "El radar ve a la persona, pero no dio estimación de respiración ahora."
      : "El anillo sigue el ritmo de respiración que estima el radar.";
    anillo.dataset.estado = rpm === null ? "persona" : "respira";
  } else {
    anillo.dataset.estado = "quieto";
    poner(est, "Sin persona detectada", false);
    est.dataset.tono = "apagado";
    motivo.textContent = ultima.objetivo
      ? "El radar tiene objetivo pero con actividad baja. No significa que la cama esté vacía."
      : "El radar no tiene objetivo. No significa que la cama esté vacía.";
  }

  const valor = $("radar-rpm");
  if (rpm !== null) {
    animarNumero(valor, rpm, rpm % 1 ? 1 : 0);
    const periodo = Math.min(12, Math.max(1.5, 60 / rpm));
    if (Math.abs((anillo._periodo || 0) - periodo) > 0.25) {
      anillo._periodo = periodo;
      anillo.style.setProperty("--periodo", `${periodo.toFixed(2)}s`);
    }
  } else {
    valor._valor = undefined;
    poner(valor, "sin dato", false);
  }

  const lat = $("radar-latido");
  if (lpm !== null) animarNumero(lat, lpm, 0, " lat/min");
  else { lat._valor = undefined; poner(lat, "sin dato", false); }

  const act = $("radar-actividad");
  if (reciente && typeof ultima.actividad === "number") animarNumero(act, ultima.actividad, 3);
  else { act._valor = undefined; poner(act, "sin dato", false); }

  const dist = $("radar-distancia");
  if (reciente && typeof ultima.distancia_cm === "number") animarNumero(dist, ultima.distancia_cm, 0, " cm");
  else { dist._valor = undefined; poner(dist, "sin dato", false); }

  $("radar-hace").textContent = ultima ? `${hace(ultima.ts, t)}${ultima.diferida ? " (diferida)" : ""}` : "—";
}

// ---------------------------------------------------------------------------
// Cinta y gráficas
// ---------------------------------------------------------------------------

function listaEventos() {
  const todos = { ...estado.recientes, ...estado.pendientes };
  return Object.entries(todos)
    .filter(([, e]) => e && typeof e === "object")
    .map(([id, e]) => ({ id, ...e }))
    .sort((a, b) => (b.timestamp || 0) - (a.timestamp || 0));
}

// Configuración de cada gráfica; la usan la vista normal y la ampliada.
const GRAFICAS = {
  resp: {
    titulo: "Respiración", subtitulo: "resp/min · estimación del radar, no validada clínicamente",
    campo: "resp_rpm", unidad: "resp/min", decimales: 1, yTopeMin: 24, nota: "estimación del radar, no validada",
    vacio: "Sin estimación de respiración en este rango",
    descripcion: "Respiración estimada por el radar, en respiraciones por minuto",
  },
  latido: {
    titulo: "Latido", subtitulo: "lat/min · estimación del radar, no validada clínicamente",
    campo: "latido_bpm", unidad: "lat/min", decimales: 0, yMin: 40, yTopeMin: 110, nota: "estimación del radar, no validada",
    vacio: "Sin estimación de latido en este rango",
    descripcion: "Latido estimado por el radar, en latidos por minuto",
  },
  actividad: {
    titulo: "Actividad", subtitulo: "movimiento visto por el radar, escala de raíz",
    campo: "actividad", unidad: "de actividad", decimales: 3, yTopeMin: 0.1, umbral: UMBRAL_ACTIVIDAD, raiz: true,
    vacio: "Sin actividad en este rango",
    descripcion: "Actividad que ve el radar; sobre el umbral se considera persona detectada",
  },
};

function pintarGraficas() {
  const hasta = ahora();
  const desde = hasta - estado.rangoMin * MINUTO;
  for (const [id, conf] of Object.entries(GRAFICAS)) {
    const figura = $(`grafica-${id}`);
    dibujarGrafica(figura.querySelector(".grafica__lienzo"), {
      ...conf, lecturas: estado.lecturas, desde, hasta,
      alAmpliar: () => abrirAmpliada(id, figura.querySelector(".ampliar")),
    });
  }
  dibujarCinta($("cinta"), {
    lecturas: estado.lecturas, eventos: listaEventos(), desde: hasta - VENTANA_MS, hasta,
    rangoDesde: desde, ahora: hasta,
  });
  redibujarAmpliada();
}

for (const b of document.querySelectorAll("[data-ampliar]")) {
  b.addEventListener("click", () => abrirAmpliada(b.dataset.ampliar, b));
}

iniciarAmpliada({
  configs: GRAFICAS,
  lecturas: () => estado.lecturas,
  ahora,
  ventanaMs: VENTANA_MS,
  rangoMs: () => estado.rangoMin * MINUTO,
  // Con la vista ampliada abierta la franja de emergencia queda en su versión delgada, encima.
  alAbrir: () => ajustarFranja(),
  alCerrar: () => ajustarFranja(),
});

function programarDibujo() {
  if (dibujoPendiente) return;
  dibujoPendiente = true;
  requestAnimationFrame(() => {
    dibujoPendiente = false;
    if (!estado.usuario) return;
    podarLecturas();
    pintarRadar();
    pintarGraficas();
  });
}

for (const b of document.querySelectorAll(".selector button")) {
  b.setAttribute("aria-checked", String(Number(b.dataset.rango) === estado.rangoMin));
  b.addEventListener("click", () => {
    estado.rangoMin = Number(b.dataset.rango);
    guardarPreferencia("rango", estado.rangoMin);
    for (const o of document.querySelectorAll(".selector button")) {
      o.setAttribute("aria-checked", String(o === b));
    }
    ocultarGlobo();
    pintarGraficas();
  });
}

let anchoPrevio = window.innerWidth;
window.addEventListener("resize", () => {
  if (window.innerWidth === anchoPrevio) return;
  anchoPrevio = window.innerWidth;
  programarDibujo();
});

// ---------------------------------------------------------------------------
// Eventos y emergencia
// ---------------------------------------------------------------------------

function alCambiarEventos() {
  pintarEventos();
  actualizarEmergencia();
  programarDibujo();
  estado.primeraCargaEventos = false;
}

function textoAtendido(e) {
  if (!e.atendido) return "";
  const quien = e.atendido_por === estado.usuario?.uid ? "ti" : e.atendido_por ? `otro cuidador (${e.atendido_por.slice(0, 6)}…)` : "—";
  const cuando = typeof e.atendido_en === "number" ? ` el ${fechaCorta(e.atendido_en)} a las ${hora(e.atendido_en)}` : "";
  return `Atendido por ${quien}${cuando}`;
}

function pintarEventos() {
  const lista = listaEventos();
  const t = ahora();
  const pendientes = lista.filter((e) => !e.atendido).length;
  $("eventos-resumen").textContent = lista.length
    ? `${pendientes ? `${pendientes} ${pendientes === 1 ? "aviso" : "avisos"} sin atender · ` : "todos atendidos · "}últimos ${Math.min(lista.length, EVENTOS_RECIENTES)}`
    : "";

  if (!lista.length) {
    $("eventos").innerHTML = `<li class="eventos__vacio">No hay eventos registrados.</li>`;
    return;
  }

  $("eventos").innerHTML = lista.map((e) => {
    const clases = ["evento"];
    if (e.tipo === "BOTON") clases.push("evento--boton");
    clases.push(e.atendido ? "evento--atendido" : "evento--pendiente");
    if (!estado.primeraCargaEventos && !estado.eventosVistos.has(e.id) && !reducirMovimiento.matches) clases.push("evento--entra");
    const ts = typeof e.timestamp === "number" ? e.timestamp : 0;
    const fecha = ts && !mismoDia(ts, t) ? `<small>${fechaCorta(ts)}</small>` : "";
    const guardando = estado.guardando.has(e.id);
    const error = estado.erroresAtender.get(e.id);
    const pie = e.atendido
      ? `<span>${escapar(textoAtendido(e))}</span>`
      : `<button type="button" class="boton boton--atender" data-atender="${escapar(e.id)}" ${guardando ? "disabled" : ""}>${guardando ? "Guardando…" : "Atendido"}</button>
         <span>sin atender · ${hace(ts, t)}</span>`;
    return `<li class="${clases.join(" ")}">
      <div class="evento__hora">${ts ? hora(ts) : "—"}${fecha}</div>
      <div>
        <div class="evento__tipo">${escapar(nombreEvento(e.tipo))}</div>
        <p class="evento__detalle">${escapar(e.detalle || "")}${e.detalle ? " · " : ""}<span class="mono">${escapar(e.origen || "")}</span></p>
      </div>
      <div class="evento__pie">${pie}${error ? `<span class="evento__error">${escapar(error)}</span>` : ""}</div>
    </li>`;
  }).join("");
  for (const e of lista) estado.eventosVistos.add(e.id);
}

$("eventos").addEventListener("click", (ev) => {
  const b = ev.target.closest("[data-atender]");
  if (b) marcarAtendido(b.dataset.atender);
});

async function marcarAtendido(id) {
  if (!estado.usuario || estado.guardando.has(id)) return;
  estado.guardando.add(id);
  estado.erroresAtender.delete(id);
  pintarEventos();
  actualizarEmergencia();
  try {
    await update(ref(db, `eventos/${id}`), {
      atendido: true,
      atendido_por: estado.usuario.uid,
      atendido_en: serverTimestamp(),
    });
  } catch (e) {
    estado.erroresAtender.set(id, /permission/i.test(e.code || e.message || "")
      ? "No se pudo guardar: sin permiso."
      : `No se pudo guardar: ${e.message || e.code}`);
  } finally {
    estado.guardando.delete(id);
    pintarEventos();
    actualizarEmergencia();
  }
}

function botonesPendientes() {
  return Object.entries(estado.pendientes)
    .filter(([, e]) => e && e.tipo === "BOTON" && !e.atendido)
    .map(([id, e]) => ({ id, ...e }))
    .sort((a, b) => (b.timestamp || 0) - (a.timestamp || 0));
}

function actualizarEmergencia() {
  const botones = estado.usuario ? botonesPendientes() : [];
  const hay = botones.length > 0;
  $("emergencia").hidden = !hay;
  document.body.classList.toggle("en-emergencia", hay);
  activarEmergencia(hay);
  if (!hay) return;
  const e = botones[0];
  const t = ahora();
  const n = botones.length;
  const cuenta = n === 1 ? "1 botón sin atender" : `${n} botones sin atender`;
  $("emergencia-cuenta").textContent = cuenta;
  $("emergencia-corto").innerHTML = `Auxilio<small>${n === 1 ? "1 botón" : `${n} botones`}</small>`;
  $("emergencia-detalle").textContent =
    `${n > 1 ? "El más reciente: " : ""}${e.origen || "colgante"} · ${hora(e.timestamp)} · ${hace(e.timestamp, t)}`;
  const todos = $("emergencia-todos");
  todos.hidden = n < 2;
  todos.textContent = `Atender todos (${n})`;
  todos.disabled = botones.some((b) => estado.guardando.has(b.id));
  const boton = $("emergencia-atender");
  boton.dataset.id = e.id;
  boton.disabled = estado.guardando.has(e.id);
  boton.textContent = estado.guardando.has(e.id) ? "Guardando…" : "Marcar atendido";
  pintarSonido();
}

function pintarSonido() {
  const s = estadoSonido();
  $("emergencia-sonido").textContent = s === "pendiente"
    ? "Toca la pantalla para activar el sonido de alerta."
    : s === "silenciado" ? "Sonido silenciado en este equipo." : "Sonando en este equipo.";
  const compacta = $("emergencia").classList.contains("emergencia--compacta");
  $("emergencia-silenciar-texto").textContent = s === "silenciado"
    ? (compacta ? "Activar" : "Activar sonido")
    : (compacta ? "Silenciar" : "Silenciar sonido");
}

alCambiar(pintarSonido);

$("emergencia-atender").addEventListener("click", (ev) => {
  const id = ev.currentTarget.dataset.id;
  if (id) marcarAtendido(id);
});

// "Atender todos": se confirma y solo incluye los botones pendientes al abrir el diálogo.
let idsPorAtender = [];
$("emergencia-todos").addEventListener("click", () => {
  idsPorAtender = botonesPendientes().map((b) => b.id);
  const n = idsPorAtender.length;
  $("confirmar-titulo").textContent = `¿Marcar como atendidos los ${n} botones sin atender?`;
  $("confirmar-si").textContent = `Sí, atender los ${n}`;
  $("confirmar-todos").showModal();
});

$("confirmar-todos").addEventListener("close", () => {
  const ids = idsPorAtender;
  idsPorAtender = [];
  if ($("confirmar-todos").returnValue === "confirmar") {
    Promise.allSettled(ids.map((id) => marcarAtendido(id)));
  }
});

// Al bajar, la franja se encoge a una barra; vuelve a su tamaño arriba. Con margen entre los dos
// umbrales para que no parpadee justo en el límite.
function ajustarFranja() {
  const franja = $("emergencia");
  const compacta = franja.classList.contains("emergencia--compacta");
  const y = estaAbierta() ? Infinity : window.scrollY;
  if (!compacta && y > 60) franja.classList.add("emergencia--compacta");
  else if (compacta && y < 12) franja.classList.remove("emergencia--compacta");
  else return;
  pintarSonido();
}
window.addEventListener("scroll", ajustarFranja, { passive: true });

// La vista ampliada empieza debajo de la franja de emergencia, que sigue visible encima.
new ResizeObserver(() => {
  const franja = $("emergencia");
  document.documentElement.style.setProperty("--alto-emergencia", `${franja.hidden ? 0 : franja.offsetHeight}px`);
}).observe($("emergencia"));

$("emergencia-silenciar").addEventListener("click", () => {
  silenciar(estadoSonido() !== "silenciado");
});

// ---------------------------------------------------------------------------
// Reloj: "hace X", estado en línea / ausente y desplazamiento de las gráficas
// ---------------------------------------------------------------------------

let tic = 0;
setInterval(() => {
  if (!estado.usuario) return;
  tic++;
  $("enc-fecha").textContent = `${fechaLarga(ahora())} · ${horaSeg(ahora())}`;
  pintarBase();
  pintarColgantes();
  if (tic % 5 === 0) programarDibujo();
  else pintarRadar();
  if (tic % 15 === 0) {
    pintarEventos();
    actualizarEmergencia();
  }
}, 1000);
