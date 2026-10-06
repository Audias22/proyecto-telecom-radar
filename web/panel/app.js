// Panel web. Por ahora vacío.
//
// TODO:
// - Importar el SDK web modular de Firebase desde el CDN de gstatic y config.js.
// - Inicio de sesión con correo y contraseña.
// - Escuchar /dispositivos/{BASE_ID}/estado y /colgantes. La base se considera desconectada si
//   ultimo_contacto tiene más de 3 minutos, aunque "online" diga true.
// - Consultar /lecturas/{BASE_ID} con orderByKey() y limitToLast() para no descargar todo el
//   historial (el plan Spark limita la descarga mensual).
// - Escuchar /eventos ordenados por timestamp y marcar atendidos (atendido, atendido_por,
//   atendido_en).
