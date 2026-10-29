// Vista ampliada de una gráfica: zoom y desplazamiento sobre la ventana de 6 h que ya está
// descargada (no hace consultas nuevas). Gestos con Pointer Events, sin librerías.

import { dibujarGrafica, detalleEn, ocultarDetalle } from "./graficas.js";
import { MINUTO, hora, horaSeg } from "./formato.js";

const SPAN_MINIMO = MINUTO;
const $ = (id) => document.getElementById(id);

let ctx = null;
let abierta = null; // id de la gráfica abierta
let vista = null; // { desde, hasta, vivo }
let origen = null; // botón que la abrió, para devolverle el foco
let pendiente = false;

// ctx: { configs, lecturas(), ahora(), ventanaMs, rangoMs(), alAbrir(), alCerrar() }
export function iniciarAmpliada(contexto) {
  ctx = contexto;
  const lienzo = $("ampliada-lienzo");

  $("ampliada-volver").addEventListener("click", volver);
  $("zoom-mas").addEventListener("click", () => zoomCentro(0.5));
  $("zoom-menos").addEventListener("click", () => zoomCentro(2));
  $("zoom-restablecer").addEventListener("click", () => {
    vista = vistaInicial();
    ocultarDetalle(lienzo);
    redibujar();
  });

  document.addEventListener("keydown", (ev) => {
    if (!abierta) return;
    if (ev.key === "Escape") {
      ev.preventDefault();
      volver();
    } else if (ev.key === "+" || ev.key === "=") zoomCentro(0.5);
    else if (ev.key === "-") zoomCentro(2);
  });

  // Atrás del celular o del navegador
  window.addEventListener("popstate", () => {
    if (abierta) cerrar();
  });

  lienzo.addEventListener("wheel", (ev) => {
    ev.preventDefault();
    const delta = ev.deltaY * (ev.deltaMode === 1 ? 33 : 1);
    zoomEn(tiempoEn(ev.clientX), Math.exp(delta * 0.0015));
  }, { passive: false });

  // Gestos: un puntero arrastra; dos (pellizco) acercan o alejan y arrastran a la vez. Un toque
  // sin moverse marca el punto. Con mouse, pasar encima también marca el punto.
  const punteros = new Map();
  let gesto = null;

  const empezarGesto = () => {
    const ps = [...punteros.values()];
    if (ps.length === 1) {
      gesto = { tipo: "arrastre", x0: ps[0].x, desde0: vista.desde, hasta0: vista.hasta, movido: false };
    } else if (ps.length >= 2) {
      const [a, b] = ps;
      gesto = {
        tipo: "pellizco",
        dist0: Math.max(10, Math.abs(a.x - b.x)),
        t0: tiempoEn((a.x + b.x) / 2),
        span0: vista.hasta - vista.desde,
        movido: true,
      };
    }
  };

  lienzo.addEventListener("pointerdown", (ev) => {
    if (ev.pointerType === "mouse" && ev.button !== 0) return;
    lienzo.setPointerCapture(ev.pointerId);
    punteros.set(ev.pointerId, { x: ev.clientX });
    empezarGesto();
  });

  lienzo.addEventListener("pointermove", (ev) => {
    if (!punteros.has(ev.pointerId)) {
      if (ev.pointerType === "mouse") detalleEn(lienzo, ev.clientX);
      return;
    }
    punteros.get(ev.pointerId).x = ev.clientX;
    if (!gesto) return;
    const esc = lienzo._escala;
    if (gesto.tipo === "arrastre") {
      const dx = ev.clientX - gesto.x0;
      if (!gesto.movido && Math.abs(dx) < 6) return;
      gesto.movido = true;
      ocultarDetalle(lienzo);
      const span = gesto.hasta0 - gesto.desde0;
      const desde = gesto.desde0 - (dx / esc.w) * span;
      aplicar(desde, desde + span);
      lienzo.classList.add("arrastrando");
    } else {
      const [a, b] = [...punteros.values()];
      const dist = Math.max(10, Math.abs(a.x - b.x));
      const span = limitarSpan(gesto.span0 * (gesto.dist0 / dist));
      // El instante que estaba entre los dedos sigue entre los dedos.
      const r = lienzo.getBoundingClientRect();
      const fraccion = ((a.x + b.x) / 2 - r.left - esc.m.izq) / esc.w;
      const desde = gesto.t0 - fraccion * span;
      ocultarDetalle(lienzo);
      aplicar(desde, desde + span);
    }
  });

  const soltar = (ev) => {
    if (!punteros.has(ev.pointerId)) return;
    const eraToque = gesto && gesto.tipo === "arrastre" && !gesto.movido && ev.type === "pointerup";
    punteros.delete(ev.pointerId);
    lienzo.classList.remove("arrastrando");
    if (eraToque) detalleEn(lienzo, ev.clientX);
    if (punteros.size) {
      // Al levantar un dedo del pellizco se sigue arrastrando con el otro, sin salto ni toque.
      empezarGesto();
      if (gesto) gesto.movido = true;
    } else {
      gesto = null;
    }
  };
  lienzo.addEventListener("pointerup", soltar);
  lienzo.addEventListener("pointercancel", soltar);
  lienzo.addEventListener("pointerleave", (ev) => {
    if (ev.pointerType === "mouse" && !punteros.size) ocultarDetalle(lienzo);
  });

  new ResizeObserver(() => abierta && redibujar()).observe(lienzo);
}

function limites() {
  const hasta = ctx.ahora();
  return { desde: hasta - ctx.ventanaMs, hasta };
}

function limitarSpan(span) {
  return Math.min(Math.max(span, SPAN_MINIMO), ctx.ventanaMs);
}

function vistaInicial() {
  const lim = limites();
  return { desde: lim.hasta - ctx.rangoMs(), hasta: lim.hasta, vivo: true };
}

// Fija la vista dentro de los límites. Si el borde derecho queda en "ahora", la vista sigue en
// vivo: avanza con los datos nuevos. Si no, se queda quieta aunque lleguen datos.
function aplicar(desde, hasta) {
  const lim = limites();
  const span = limitarSpan(hasta - desde);
  let d = desde;
  if (d + span > lim.hasta) d = lim.hasta - span;
  if (d < lim.desde) d = lim.desde;
  vista = { desde: d, hasta: d + span, vivo: d + span >= lim.hasta - 1000 };
  programar();
}

function tiempoEn(clientX) {
  const lienzo = $("ampliada-lienzo");
  const esc = lienzo._escala;
  const r = lienzo.getBoundingClientRect();
  const f = Math.min(Math.max((clientX - r.left - esc.m.izq) / esc.w, 0), 1);
  return vista.desde + f * (vista.hasta - vista.desde);
}

function zoomEn(t, factor) {
  const span = limitarSpan((vista.hasta - vista.desde) * factor);
  const f = (t - vista.desde) / (vista.hasta - vista.desde);
  const desde = t - f * span;
  // En vivo y acercando desde el botón o la rueda cerca del borde, se mantiene en vivo.
  aplicar(desde, desde + span);
}

function zoomCentro(factor) {
  if (!abierta) return;
  const t = vista.vivo ? vista.hasta : (vista.desde + vista.hasta) / 2;
  zoomEn(t, factor);
}

function programar() {
  if (pendiente) return;
  pendiente = true;
  requestAnimationFrame(() => {
    pendiente = false;
    redibujar();
  });
}

function duracion(ms) {
  const s = Math.round(ms / 1000);
  if (s < 60) return `${s} s`;
  const m = Math.round(s / 60);
  if (m < 60) return `${m} min`;
  const h = Math.floor(m / 60);
  return m % 60 ? `${h} h ${m % 60} min` : `${h} h`;
}

// Se llama al abrir, en cada gesto y cada vez que llegan datos (desde app.js).
export function redibujar() {
  if (!abierta) return;
  const lim = limites();
  if (vista.vivo) {
    const span = vista.hasta - vista.desde;
    vista = { desde: lim.hasta - span, hasta: lim.hasta, vivo: true };
  } else if (vista.desde < lim.desde) {
    aplicar(lim.desde, lim.desde + (vista.hasta - vista.desde));
  }
  const conf = ctx.configs[abierta];
  dibujarGrafica($("ampliada-lienzo"), {
    ...conf, lecturas: ctx.lecturas(), desde: vista.desde, hasta: vista.hasta, propia: true,
  });
  const span = vista.hasta - vista.desde;
  const fmt = span < 10 * MINUTO ? horaSeg : hora;
  $("ampliada-rango").textContent = `${fmt(vista.desde)} – ${fmt(vista.hasta)} · ${duracion(span)}`;
  const estado = $("ampliada-vivo");
  estado.textContent = vista.vivo ? "en vivo" : "detenida en este tramo";
  estado.dataset.vivo = String(vista.vivo);
  $("zoom-mas").disabled = span <= SPAN_MINIMO;
  $("zoom-menos").disabled = span >= ctx.ventanaMs;
}

export function abrirAmpliada(id, boton) {
  if (!ctx.configs[id]) return;
  const yaAbierta = abierta !== null;
  abierta = id;
  origen = boton || document.activeElement;
  vista = vistaInicial();
  const conf = ctx.configs[id];
  $("ampliada-titulo").textContent = conf.titulo;
  $("ampliada-sub").textContent = conf.subtitulo;
  $("ampliada-umbral").hidden = typeof conf.umbral !== "number";
  ocultarDetalle();
  $("ampliada").hidden = false;
  document.body.classList.add("con-ampliada");
  if (!yaAbierta) history.pushState({ ampliada: id }, "");
  ctx.alAbrir();
  redibujar();
  $("ampliada-volver").focus();
}

export function estaAbierta() {
  return abierta !== null;
}

// "← Volver" y Esc pasan por el historial, así Atrás y Volver hacen lo mismo.
function volver() {
  if (history.state && history.state.ampliada) history.back();
  else cerrar();
}

function cerrar() {
  abierta = null;
  ocultarDetalle();
  $("ampliada").hidden = true;
  document.body.classList.remove("con-ampliada");
  ctx.alCerrar();
  origen?.focus?.();
}
