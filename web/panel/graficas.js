// Gráficas de línea y "cinta" de presencia en SVG propio (sin librerías).

import { MINUTO, HORA, hora, horaSeg, hace, numero, escapar, nombreEvento, UMBRAL_ACTIVIDAD } from "./formato.js";

const NS = "http://www.w3.org/2000/svg";

// ---------------------------------------------------------------------------
// Globo (tooltip) compartido
// ---------------------------------------------------------------------------

const globo = document.getElementById("globo");

export function mostrarGlobo(html, x, y) {
  globo.innerHTML = html;
  globo.hidden = false;
  const r = globo.getBoundingClientRect();
  let left = x + 14;
  let top = y - r.height - 12;
  if (left + r.width > window.innerWidth - 8) left = x - r.width - 14;
  if (left < 8) left = 8;
  if (top < 8) top = y + 16;
  globo.style.left = `${left}px`;
  globo.style.top = `${top}px`;
}

export function ocultarGlobo() {
  globo.hidden = true;
}

function el(nombre, atributos = {}, padre) {
  const nodo = document.createElementNS(NS, nombre);
  for (const [k, v] of Object.entries(atributos)) nodo.setAttribute(k, v);
  if (padre) padre.appendChild(nodo);
  return nodo;
}

// Índice de la primera lectura con ts >= t (lecturas ordenadas por ts).
export function buscarDesde(lecturas, t) {
  let lo = 0;
  let hi = lecturas.length;
  while (lo < hi) {
    const m = (lo + hi) >> 1;
    if (lecturas[m].ts < t) lo = m + 1;
    else hi = m;
  }
  return lo;
}

function pasoMarcas(rango) {
  if (rango <= 20 * MINUTO) return 5 * MINUTO;
  if (rango <= 90 * MINUTO) return 15 * MINUTO;
  if (rango <= 3 * HORA) return 30 * MINUTO;
  return HORA;
}

function marcasY(lo, hi) {
  const crudo = (hi - lo) / 3;
  const pot = 10 ** Math.floor(Math.log10(crudo));
  const paso = [1, 2, 2.5, 5, 10].map((f) => f * pot).find((p) => p >= crudo);
  const marcas = [];
  for (let v = Math.ceil(lo / paso) * paso; v <= hi + 1e-9; v += paso) marcas.push(+v.toFixed(6));
  return { marcas, tope: Math.max(hi, marcas[marcas.length - 1]) };
}

function medianas(puntos, desde, paso) {
  const salida = [];
  let grupo = [];
  let clave = null;
  const cerrar = () => {
    if (!grupo.length) return;
    const vs = grupo.map((p) => p.v).sort((a, b) => a - b);
    const k = vs.length >> 1;
    const v = vs.length % 2 ? vs[k] : (vs[k - 1] + vs[k]) / 2;
    const ts = Math.round(grupo.reduce((s, p) => s + p.ts, 0) / grupo.length);
    salida.push({ ts, v, n: grupo.length });
    grupo = [];
  };
  for (const p of puntos) {
    const c = Math.floor((p.ts - desde) / paso);
    if (c !== clave) {
      cerrar();
      clave = c;
    }
    grupo.push(p);
  }
  cerrar();
  return salida;
}

// ---------------------------------------------------------------------------
// Gráfica de línea
// ---------------------------------------------------------------------------

// opciones: { lecturas, desde, hasta, campo, unidad, decimales, yMin, yTopeMin, umbral, vacio }
export function dibujarGrafica(lienzo, opciones) {
  lienzo._opciones = opciones;
  const ancho = lienzo.clientWidth;
  const alto = lienzo.clientHeight;
  if (!ancho || !alto) return;

  const { lecturas, desde, hasta, campo, decimales = 0 } = opciones;
  const m = { izq: 36, der: 10, arr: 10, aba: 22 };
  const w = ancho - m.izq - m.der;
  const h = alto - m.arr - m.aba;

  const i0 = buscarDesde(lecturas, desde);
  let puntos = [];
  for (let i = i0; i < lecturas.length && lecturas[i].ts <= hasta; i++) {
    const v = lecturas[i][campo];
    if (typeof v === "number") puntos.push({ ts: lecturas[i].ts, v, n: 1 });
  }
  // Con más lecturas que medio píxel por punto, cada punto es la mediana de su tramo de tiempo:
  // a 1 h y 6 h el trazo crudo cada 5 s es ilegible en un celular.
  if (puntos.length > w / 2) puntos = medianas(puntos, desde, (hasta - desde) / (w / 2));
  lienzo._puntos = puntos;

  let maximo = opciones.yTopeMin;
  for (const p of puntos) if (p.v > maximo) maximo = p.v;
  const yMin = opciones.yMin ?? 0;
  let { marcas, tope } = marcasY(yMin, maximo * (puntos.length ? 1.08 : 1));
  // Escala de raíz (actividad): deja ver el umbral de 0.03 aunque haya picos de 1 o más.
  const f = opciones.raiz ? Math.sqrt : (v) => v;
  if (opciones.raiz) {
    tope = maximo * (puntos.length ? 1.08 : 1);
    marcas = [0, 0.03, 0.1, 0.3, 1, 3, 10].filter((v) => v <= tope);
  }

  const x = (t) => m.izq + ((t - desde) / (hasta - desde)) * w;
  const y = (v) => m.arr + h - ((f(v) - f(yMin)) / (f(tope) - f(yMin))) * h;
  lienzo._escala = { x, y, desde, hasta, m, w };

  const svg = el("svg", { viewBox: `0 0 ${ancho} ${alto}`, role: "img", "aria-label": opciones.descripcion || "" });

  // Ejes
  const ejes = el("g", { class: "eje" }, svg);
  for (const v of marcas) {
    el("line", { x1: m.izq, x2: m.izq + w, y1: y(v), y2: y(v) }, ejes);
    const t = el("text", { x: m.izq - 6, y: y(v) + 3.5, "text-anchor": "end" }, ejes);
    t.textContent = numero(v, v % 1 ? (v < 0.1 ? 2 : 1) : 0);
    if (opciones.raiz && v === 0.03) t.setAttribute("visibility", "hidden"); // ya lo nombra el umbral
  }
  const paso = pasoMarcas(hasta - desde);
  for (let t = Math.ceil(desde / paso) * paso; t <= hasta; t += paso) {
    const tx = el("text", { x: x(t), y: alto - 6, "text-anchor": "middle" }, ejes);
    tx.textContent = hora(t);
  }

  if (typeof opciones.umbral === "number" && opciones.umbral < tope) {
    el("line", { class: "umbral", x1: m.izq, x2: m.izq + w, y1: y(opciones.umbral), y2: y(opciones.umbral) }, svg);
    const t = el("text", { class: "umbral-texto", x: m.izq + 4, y: y(opciones.umbral) - 4 }, svg);
    t.textContent = `umbral ${numero(opciones.umbral, 2)}`;
  }

  // Trazo: se corta donde faltan datos para no inventar valores entre huecos.
  const corte = Math.max(30 * 1000, (hasta - desde) / 120);
  let d = "";
  let area = "";
  let tramo = [];
  const cerrarTramo = () => {
    if (tramo.length === 1) {
      el("circle", { class: "cursor-punto", cx: x(tramo[0].ts), cy: y(tramo[0].v), r: 2.5 }, svg);
    } else if (tramo.length > 1) {
      const seg = tramo.map((p, i) => `${i ? "L" : "M"}${x(p.ts).toFixed(1)},${y(p.v).toFixed(1)}`).join("");
      d += seg;
      area += `${seg}L${x(tramo[tramo.length - 1].ts).toFixed(1)},${y(yMin)}L${x(tramo[0].ts).toFixed(1)},${y(yMin)}Z`;
    }
    tramo = [];
  };
  for (const p of puntos) {
    if (tramo.length && p.ts - tramo[tramo.length - 1].ts > corte) cerrarTramo();
    tramo.push(p);
  }
  cerrarTramo();
  if (area) el("path", { class: "trazo-area", d: area }, svg);
  if (d) el("path", { class: "trazo", d }, svg);

  if (!puntos.length) {
    const t = el("text", { class: "vacio", x: m.izq + w / 2, y: m.arr + h / 2, "text-anchor": "middle" }, svg);
    t.textContent = opciones.vacio || "Sin datos en este rango";
  }

  const cursor = el("g", { visibility: "hidden" }, svg);
  el("line", { class: "cursor-linea", y1: m.arr, y2: m.arr + h }, cursor);
  el("circle", { class: "cursor-punto", r: 4.5 }, cursor);
  lienzo._cursor = cursor;

  lienzo.replaceChildren(svg);

  if (!lienzo._escuchando) {
    lienzo._escuchando = true;
    const mover = (ev) => moverCursor(lienzo, ev);
    lienzo.addEventListener("pointermove", mover);
    lienzo.addEventListener("pointerdown", mover);
    lienzo.addEventListener("pointerleave", () => {
      lienzo._cursor?.setAttribute("visibility", "hidden");
      ocultarGlobo();
    });
  }
}

function moverCursor(lienzo, ev) {
  const puntos = lienzo._puntos;
  const esc = lienzo._escala;
  const op = lienzo._opciones;
  if (!puntos || !esc) return;
  const r = lienzo.getBoundingClientRect();
  const px = ev.clientX - r.left;
  const t = esc.desde + ((px - esc.m.izq) / esc.w) * (esc.hasta - esc.desde);
  if (!puntos.length || px < esc.m.izq - 4 || px > esc.m.izq + esc.w + 4) {
    lienzo._cursor.setAttribute("visibility", "hidden");
    ocultarGlobo();
    return;
  }
  let i = buscarDesde(puntos, t);
  if (i >= puntos.length || (i > 0 && t - puntos[i - 1].ts < puntos[i].ts - t)) i -= 1;
  const p = puntos[Math.max(0, i)];
  const tolerancia = Math.max(30 * 1000, (esc.hasta - esc.desde) / 60);
  if (Math.abs(p.ts - t) > tolerancia) {
    lienzo._cursor.setAttribute("visibility", "hidden");
    mostrarGlobo(`<b>${horaSeg(t)}</b><small>sin dato del radar</small>`, ev.clientX, ev.clientY);
    return;
  }
  const [linea, punto] = lienzo._cursor.children;
  linea.setAttribute("x1", esc.x(p.ts));
  linea.setAttribute("x2", esc.x(p.ts));
  punto.setAttribute("cx", esc.x(p.ts));
  punto.setAttribute("cy", esc.y(p.v));
  lienzo._cursor.setAttribute("visibility", "visible");
  const rp = lienzo.getBoundingClientRect();
  const tramo = p.n > 1 ? `<small>mediana de ${p.n} lecturas</small>` : "";
  mostrarGlobo(
    `<b>${numero(p.v, op.decimales)} ${escapar(op.unidad)}</b><small>${horaSeg(p.ts)}${op.nota ? " · " + escapar(op.nota) : ""}</small>${tramo}`,
    rp.left + esc.x(p.ts), rp.top + esc.y(p.v));
}

// ---------------------------------------------------------------------------
// Cinta de presencia con eventos clavados encima
// ---------------------------------------------------------------------------

const ESTADOS_CINTA = {
  persona: "persona detectada",
  baja: "radar con objetivo, actividad baja",
  sin: "radar sin objetivo",
  hueco: "sin lecturas",
};

function estadoMinuto(c) {
  if (!c || !c.n) return "hueco";
  if (c.persona * 2 >= c.n) return "persona";
  if (c.objetivo) return "baja";
  return "sin";
}

// opciones: { lecturas, eventos, desde, hasta, rangoDesde, ahora }
export function dibujarCinta(contenedor, opciones) {
  contenedor._opciones = opciones;
  const ancho = contenedor.clientWidth;
  if (!ancho) return;
  const { lecturas, eventos, desde, hasta } = opciones;
  const alto = 104;
  const m = { izq: 4, der: 4 };
  const w = ancho - m.izq - m.der;
  const banda = { y: 38, h: 40 };
  const x = (t) => m.izq + ((t - desde) / (hasta - desde)) * w;

  // Minutos (1 min por celda)
  const n = Math.ceil((hasta - desde) / MINUTO);
  const celdas = new Array(n);
  for (let i = buscarDesde(lecturas, desde); i < lecturas.length && lecturas[i].ts <= hasta; i++) {
    const l = lecturas[i];
    const k = Math.min(n - 1, Math.floor((l.ts - desde) / MINUTO));
    const c = celdas[k] || (celdas[k] = { n: 0, persona: 0, objetivo: 0, actividad: null });
    c.n++;
    if (l.persona) c.persona++;
    if (l.objetivo) c.objetivo++;
    if (typeof l.actividad === "number") c.actividad = Math.max(c.actividad ?? 0, l.actividad);
  }
  contenedor._celdas = celdas;
  contenedor._escala = { x, desde, hasta, w, m, n };

  const svg = el("svg", { viewBox: `0 0 ${ancho} ${alto}`, role: "img",
    "aria-label": "Presencia detectada por el radar en las últimas 6 horas, con los eventos marcados" });

  el("rect", { class: "cinta__fondo", x: m.izq, y: banda.y, width: w, height: banda.h, rx: 3 }, svg);

  // Marcas de hora
  for (let t = Math.ceil(desde / HORA) * HORA; t <= hasta; t += HORA) {
    el("line", { class: "cinta__marca-hora", x1: x(t), x2: x(t), y1: banda.y, y2: banda.y + banda.h + 6 }, svg);
    const tx = el("text", { class: "cinta__hora", x: x(t), y: alto - 8, "text-anchor": "middle" }, svg);
    tx.textContent = hora(t);
  }

  // Tramos: minutos seguidos con el mismo estado se unen en un rectángulo.
  const altoEstado = { persona: 1, baja: 0.55, sin: 0.22 };
  let i = 0;
  while (i < n) {
    const estado = estadoMinuto(celdas[i]);
    let j = i + 1;
    while (j < n && estadoMinuto(celdas[j]) === estado) j++;
    const x0 = x(desde + i * MINUTO);
    const x1 = x(Math.min(hasta, desde + j * MINUTO));
    if (estado === "hueco") {
      const ym = banda.y + banda.h / 2;
      if (x1 - x0 > 3) el("line", { class: "cinta__hueco", x1: x0 + 1, x2: x1 - 1, y1: ym, y2: ym }, svg);
    } else {
      const hh = banda.h * altoEstado[estado];
      el("rect", { class: `cinta__${estado}`, x: x0, y: banda.y + banda.h - hh, width: Math.max(1, x1 - x0), height: hh }, svg);
    }
    i = j;
  }

  // Rango que muestran las gráficas
  if (opciones.rangoDesde > desde) {
    const rx = x(opciones.rangoDesde);
    el("rect", { class: "cinta__rango", x: rx, y: banda.y - 3, width: x(hasta) - rx, height: banda.h + 6, rx: 3 }, svg);
  }
  el("line", { class: "cinta__ahora", x1: x(hasta), x2: x(hasta), y1: banda.y - 6, y2: banda.y + banda.h + 6 }, svg);

  // Zona sensible y cursor
  const zona = el("rect", { class: "cinta__zona", x: m.izq, y: banda.y - 4, width: w, height: banda.h + 8 }, svg);
  const cursor = el("line", { class: "cinta__cursor", y1: banda.y - 4, y2: banda.y + banda.h + 4, visibility: "hidden" }, svg);
  const moverZona = (ev) => {
    const r = svg.getBoundingClientRect();
    const px = (ev.clientX - r.left) * (ancho / r.width);
    const k = Math.floor(((px - m.izq) / w) * n);
    if (k < 0 || k >= n) return;
    const c = celdas[k];
    const t0 = desde + k * MINUTO;
    cursor.setAttribute("x1", px);
    cursor.setAttribute("x2", px);
    cursor.setAttribute("visibility", "visible");
    const estado = estadoMinuto(c);
    let extra = "";
    if (c && c.n) {
      extra = `${c.n} lectura${c.n === 1 ? "" : "s"}`;
      if (c.actividad !== null) extra += ` · actividad máx. ${numero(c.actividad, 3)}`;
    }
    mostrarGlobo(`<b>${hora(t0)}</b> ${escapar(ESTADOS_CINTA[estado])}${extra ? `<small>${extra}</small>` : ""}`, ev.clientX, ev.clientY);
  };
  zona.addEventListener("pointermove", moverZona);
  zona.addEventListener("pointerdown", moverZona);
  zona.addEventListener("pointerleave", () => {
    cursor.setAttribute("visibility", "hidden");
    ocultarGlobo();
  });

  // Eventos clavados encima de la cinta
  const visibles = eventos.filter((e) => e.timestamp >= desde && e.timestamp <= hasta)
    .sort((a, b) => a.timestamp - b.timestamp);
  for (const e of visibles) {
    const ex = x(e.timestamp);
    const clases = ["cinta__evento"];
    if (e.tipo === "BOTON") clases.push("cinta__evento--boton");
    if (!e.atendido) clases.push("cinta__evento--pendiente");
    const g = el("g", { class: clases.join(" "), tabindex: "0", role: "button",
      "aria-label": `${nombreEvento(e.tipo)} a las ${hora(e.timestamp)}` }, svg);
    el("line", { x1: ex, x2: ex, y1: 16, y2: banda.y + banda.h }, g);
    el("circle", { cx: ex, cy: 14, r: 6 }, g);
    el("rect", { class: "cinta__blanco", x: ex - 12, y: 0, width: 24, height: banda.y }, g);
    const html = () =>
      `<b>${hora(e.timestamp)}</b> ${escapar(nombreEvento(e.tipo))}` +
      `<small>${escapar(e.detalle || e.origen || "")}</small>` +
      `<small>${e.atendido ? "atendido" : "sin atender"} · ${hace(e.timestamp, opciones.ahora)}</small>`;
    const mostrar = () => {
      const r = g.querySelector("circle").getBoundingClientRect();
      mostrarGlobo(html(), r.left + r.width / 2, r.top);
    };
    g.addEventListener("pointerenter", mostrar);
    g.addEventListener("pointerdown", mostrar);
    g.addEventListener("focus", mostrar);
    g.addEventListener("pointerleave", ocultarGlobo);
    g.addEventListener("blur", ocultarGlobo);
  }

  contenedor.replaceChildren(svg);
}
