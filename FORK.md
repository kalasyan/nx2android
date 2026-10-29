# kalasyan/nx2android

Fork de [shirai91/nx2android](https://github.com/shirai91/nx2android) con tres añadidos:

- **Carpeta de datos libre.** La app lee el paquete de datos de `/sdcard/nx2android/<paquete>/` (por ejemplo `/sdcard/nx2android/org.nx2a.donkeykongcountryreturnshd/`), que se puede llenar con cualquier explorador de archivos, sin adb ni Shizuku. La primera vez Android pide el permiso de **acceso a todos los archivos**: concédelo y vuelve a abrir la app. Si la carpeta de siempre (`Android/data/<paquete>/files`) ya tiene un paquete de datos, se usa esa, así que `porter install` sigue funcionando igual. Copia ahí **el contenido** de la carpeta `<App>-data` que genera el port. Cambios: `nxnative/dm/android/front_android.c` (`pick_root`) y `nxnative/dm/android/apk/mkapk.py` (permisos).
- **Sincronización diaria** (`.github/workflows/sync-upstream.yml`, 05:17 UTC o a mano desde Actions): fusiona el `main` del autor en este `main`. Si hay conflicto no toca nada y abre un issue.
- **nx2android.exe propio** (`.github/workflows/build-exe.yml`): tras cada sincronización con cambios o cada push a `main`, compila el bundle de Windows como el autor y lo publica en [Releases](../../releases) como `fork-<versión>`.
