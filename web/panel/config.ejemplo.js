// Configuración web de Firebase. Copiar como config.js y llenar con los datos de
// Consola de Firebase > Configuración del proyecto > Tus apps > App web.
// Estos valores identifican el proyecto pero no dan acceso: la seguridad la ponen las reglas
// de database.rules.json y las cuentas de Firebase Authentication.
// config.js no se sube al repositorio para que cada quien pueda apuntar a su proyecto de prueba.

export const firebaseConfig = {
  apiKey: "api_key_web_del_proyecto",
  authDomain: "id-del-proyecto.firebaseapp.com",
  databaseURL: "https://id-del-proyecto-default-rtdb.firebaseio.com",
  projectId: "id-del-proyecto",
  appId: "app_id_del_proyecto",
};

// Base que muestra el panel.
export const BASE_ID = "base01";
