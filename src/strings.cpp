#include "strings.h"

#include <windows.h>

#include <cstdio>
#include <iterator>

namespace {

constexpr const wchar_t* kEn[] = {
    L"Active: no ad playing",                                                          // StatusActive
    L"Ad: muted",                                                                       // StatusMuted
    L"Ad: volume lowered (%d%%)",                                                       // StatusLowered
    L"Video: muted",                                                                    // StatusMediaMuted
    L"Video: volume lowered (%d%%)",                                                    // StatusMediaLowered
    L"Video: Spotify paused",                                                           // StatusMediaPaused
    L"Spotify not found",                                                               // StatusNoSpotify
    L"Disabled",                                                                        // StatusDisabled
    L"Enabled",                                                                         // MenuEnabled
    L"Settings",                                                                       // MenuSettings
    L"Exit",                                                                            // MenuExit
    L"Hide from tray",  // MenuHideTray
    L"AdMutify Settings",                                                               // SettingsTitle
    L"Ads muted",  // AdsMutedCaption
    L"On",  // ToggleOn
    L"Off",  // ToggleOff
    L"Start with Windows",                                                              // OptAutostart
    L"No tray icon when starting with Windows",                                         // OptHideTray
    L"Applies when AdMutify starts with Windows. Open AdMutify again to show the icon.", // OptHideTrayHint
    L"During ads",                                                                      // GroupAds
    L"Mute",                                                                            // ModeMute
    L"Lower volume",                                                                    // ModeLower
    L"Play your own music",  // ModeMusic
    L"%d %%",                                                                           // PercentValue
    L"During videos in other apps",                                                     // MediaLabel
    L"Do nothing",                                                                      // MediaOff
    L"Lower Spotify",                                                                   // MediaLower
    L"Mute Spotify",                                                                    // MediaMute
    L"Pause Spotify",                                                                   // MediaPause
    L"Reacts only to sound from browsers and video players that lasts a few seconds. Short sounds, games and calls are ignored.",  // MediaHint
    L"Lower to",                                                                        // MediaPercentLabel
    L"Source",  // MusicSource
    L"One file",  // MusicFile
    L"Folder, shuffled",  // MusicShuffle
    L"Folder, in order",  // MusicOrder
    L"Plays your own music during ads. When the ad ends, the music pauses and continues at the same spot during the next ad.",  // MusicHint
    L"No file chosen",  // MusicNoFile
    L"No folder chosen",  // MusicNoFolder
    L"Choose",  // MusicPick
    L"Volume",  // MusicVolumeLabel
    L"Choose a music file",  // MusicPickFileTitle
    L"Choose a music folder",  // MusicPickFolderTitle
    L"Music files",  // MusicFilterName
    L"This folder has no playable music files (%s). Choose another folder.",  // ErrMusicEmpty
    L"Language",                                                                        // LanguageLabel
    L"System",                                                                          // LangSystem
    L"Reset settings",                                                                  // ResetButton
    L"Click again to reset",                                                            // ResetConfirmClick
    L"Close",                                                                           // Close
    L"Your settings file comes from a newer AdMutify version. Defaults are used and the file is left unchanged.",  // ErrSettingsNewer
    L"Could not save the settings. Check that the folder is writable.",                 // ErrSettingsSave
    L"Could not change the Windows startup entry.",                                     // ErrAutostart
    L"Win32 error %lu: %s",                                                             // ErrTechnical
    L"AdMutify closed unexpectedly",                                                    // CrashTitle
    L"Spotify's volume was restored. If this keeps happening, send the details to the developer.",  // CrashBody
    L"Spotify's volume could not be restored. Check it in the Windows volume mixer. If this keeps happening, send the details to the developer.",  // CrashBodyNotRestored
    L"Copy details",                                                                    // CrashCopy
    L"Open log folder",                                                                 // CrashOpenLogs
};

constexpr const wchar_t* kDe[] = {
    L"Aktiv: keine Werbung",                                                            // StatusActive
    L"Werbung: stumm",                                                                  // StatusMuted
    L"Werbung: leiser (%d %%)",                                                         // StatusLowered
    L"Video: stumm",                                                                    // StatusMediaMuted
    L"Video: leiser (%d %%)",                                                           // StatusMediaLowered
    L"Video: Spotify pausiert",                                                         // StatusMediaPaused
    L"Spotify nicht gefunden",                                                          // StatusNoSpotify
    L"Deaktiviert",                                                                     // StatusDisabled
    L"Aktiv",                                                                           // MenuEnabled
    L"Einstellungen",                                                                  // MenuSettings
    L"Beenden",                                                                         // MenuExit
    L"Im Tray ausblenden",  // MenuHideTray
    L"AdMutify Einstellungen",                                                          // SettingsTitle
    L"Werbungen stummgeschaltet",  // AdsMutedCaption
    L"An",  // ToggleOn
    L"Aus",  // ToggleOff
    L"Mit Windows starten",                                                             // OptAutostart
    L"Beim Start mit Windows ohne Tray-Icon",                                           // OptHideTray
    L"Gilt beim Start mit Windows. Zum Anzeigen AdMutify erneut öffnen.",            // OptHideTrayHint
    L"Bei Werbung",                                                                     // GroupAds
    L"Stummschalten",                                                                   // ModeMute
    L"Leiser stellen",                                                                  // ModeLower
    L"Eigene Musik spielen",  // ModeMusic
    L"%d %%",                                                                           // PercentValue
    L"Bei Videos in anderen Apps",                                                      // MediaLabel
    L"Nichts tun",                                                                      // MediaOff
    L"Spotify leiser",                                                                  // MediaLower
    L"Spotify stumm",                                                                   // MediaMute
    L"Spotify pausieren",                                                               // MediaPause
    L"Reagiert nur auf Ton von Browsern und Videoplayern, der einige Sekunden anhält. Kurze Töne, Spiele und Anrufe zählen nicht.",  // MediaHint
    L"Leiser auf",                                                                      // MediaPercentLabel
    L"Quelle",  // MusicSource
    L"Eine Datei",  // MusicFile
    L"Ordner, zufällig",  // MusicShuffle
    L"Ordner, der Reihe nach",  // MusicOrder
    L"Spielt während der Werbung eigene Musik ab. Endet die Werbung, pausiert die Musik und läuft bei der nächsten Werbung an derselben Stelle weiter.",  // MusicHint
    L"Keine Datei gewählt",  // MusicNoFile
    L"Kein Ordner gewählt",  // MusicNoFolder
    L"Auswählen",  // MusicPick
    L"Lautstärke",  // MusicVolumeLabel
    L"Musikdatei auswählen",  // MusicPickFileTitle
    L"Musikordner auswählen",  // MusicPickFolderTitle
    L"Musikdateien",  // MusicFilterName
    L"In diesem Ordner sind keine abspielbaren Musikdateien (%s). Wähle einen anderen Ordner.",  // ErrMusicEmpty
    L"Sprache",                                                                         // LanguageLabel
    L"System",                                                                          // LangSystem
    L"Einstellungen zurücksetzen",                                                 // ResetButton
    L"Zum Zurücksetzen erneut klicken",                                            // ResetConfirmClick
    L"Schließen",                                                                  // Close
    L"Die Einstellungsdatei stammt aus einer neueren AdMutify-Version. Es gelten die Standardwerte, die Datei bleibt unverändert.",  // ErrSettingsNewer
    L"Die Einstellungen konnten nicht gespeichert werden. Prüfe, ob der Ordner beschreibbar ist.",  // ErrSettingsSave
    L"Der Autostart-Eintrag konnte nicht geändert werden.",                        // ErrAutostart
    L"Win32-Fehler %lu: %s",                                                            // ErrTechnical
    L"AdMutify wurde unerwartet beendet",                                               // CrashTitle
    L"Die Spotify-Lautstärke wurde wiederhergestellt. Passiert das öfter, schick die Details an den Entwickler.",  // CrashBody
    L"Die Spotify-Lautstärke konnte nicht wiederhergestellt werden. Prüfe sie im Lautstärkemixer von Windows. Passiert das öfter, schick die Details an den Entwickler.",  // CrashBodyNotRestored
    L"Details kopieren",                                                                // CrashCopy
    L"Log-Ordner öffnen",                                                          // CrashOpenLogs
};

constexpr size_t kCount = static_cast<size_t>(StrId::Count);
static_assert(std::size(kEn) == kCount, "English table size mismatch");
static_assert(std::size(kDe) == kCount, "German table size mismatch");

// BEGIN GENERATED TABLES

constexpr const wchar_t* kEs[] = {  // Spanish
    L"Activo: no hay anuncios",  // StatusActive
    L"Anuncio: silenciado",  // StatusMuted
    L"Anuncio: volumen reducido (%d %%)",  // StatusLowered
    L"Vídeo: silenciado",  // StatusMediaMuted
    L"Vídeo: volumen reducido (%d %%)",  // StatusMediaLowered
    L"Vídeo: Spotify en pausa",  // StatusMediaPaused
    L"No se encontró Spotify",  // StatusNoSpotify
    L"Desactivado",  // StatusDisabled
    L"Activado",  // MenuEnabled
    L"Configuración",  // MenuSettings
    L"Salir",  // MenuExit
    L"Ocultar de la bandeja",  // MenuHideTray
    L"Configuración de AdMutify",  // SettingsTitle
    L"Anuncios silenciados",  // AdsMutedCaption
    L"Encendido",  // ToggleOn
    L"Apagado",  // ToggleOff
    L"Iniciar con Windows",  // OptAutostart
    L"Sin icono en el área de notificación al iniciar con Windows",  // OptHideTray
    L"Se aplica cuando AdMutify se inicia con Windows. Abre AdMutify de nuevo para mostrar el icono.",  // OptHideTrayHint
    L"Durante los anuncios",  // GroupAds
    L"Silenciar",  // ModeMute
    L"Reducir volumen",  // ModeLower
    L"Reproducir tu propia música",  // ModeMusic
    L"%d %%",  // PercentValue
    L"Durante vídeos en otras aplicaciones",  // MediaLabel
    L"No hacer nada",  // MediaOff
    L"Bajar volumen de Spotify",  // MediaLower
    L"Silenciar Spotify",  // MediaMute
    L"Pausar Spotify",  // MediaPause
    L"Solo reacciona al sonido de navegadores y reproductores de vídeo que dura unos segundos. Se ignoran los sonidos breves, los juegos y las llamadas.",  // MediaHint
    L"Reducir a",  // MediaPercentLabel
    L"Fuente",  // MusicSource
    L"Un archivo",  // MusicFile
    L"Carpeta, aleatorio",  // MusicShuffle
    L"Carpeta, en orden",  // MusicOrder
    L"Reproduce tu propia música durante los anuncios. Cuando termina el anuncio, la música se pausa y continúa en el mismo punto en el siguiente anuncio.",  // MusicHint
    L"Ningún archivo elegido",  // MusicNoFile
    L"Ninguna carpeta elegida",  // MusicNoFolder
    L"Elegir",  // MusicPick
    L"Volumen",  // MusicVolumeLabel
    L"Elegir un archivo de música",  // MusicPickFileTitle
    L"Elegir una carpeta de música",  // MusicPickFolderTitle
    L"Archivos de música",  // MusicFilterName
    L"Esta carpeta no tiene archivos de música reproducibles (%s). Elige otra carpeta.",  // ErrMusicEmpty
    L"Idioma",  // LanguageLabel
    L"Sistema",  // LangSystem
    L"Restablecer configuración",  // ResetButton
    L"Haz clic de nuevo para restablecer",  // ResetConfirmClick
    L"Cerrar",  // Close
    L"El archivo de configuración procede de una versión más reciente de AdMutify. Se usan los valores predeterminados y el archivo no se modifica.",  // ErrSettingsNewer
    L"No se pudo guardar la configuración. Comprueba que se pueda escribir en la carpeta.",  // ErrSettingsSave
    L"No se pudo cambiar la entrada de inicio de Windows.",  // ErrAutostart
    L"Error de Win32 %lu: %s",  // ErrTechnical
    L"AdMutify se cerró inesperadamente",  // CrashTitle
    L"Se restauró el volumen de Spotify. Si esto sigue ocurriendo, envía los detalles al desarrollador.",  // CrashBody
    L"No se pudo restaurar el volumen de Spotify. Revísalo en el mezclador de volumen de Windows. Si esto sigue ocurriendo, envía los detalles al desarrollador.",  // CrashBodyNotRestored
    L"Copiar detalles",  // CrashCopy
    L"Abrir carpeta de registros",  // CrashOpenLogs
};
static_assert(std::size(kEs) == kCount, "Spanish table size mismatch");

constexpr const wchar_t* kPt[] = {  // Brazilian Portuguese
    L"Ativo: nenhum anúncio",  // StatusActive
    L"Anúncio: sem som",  // StatusMuted
    L"Anúncio: volume reduzido (%d%%)",  // StatusLowered
    L"Vídeo: sem som",  // StatusMediaMuted
    L"Vídeo: volume reduzido (%d%%)",  // StatusMediaLowered
    L"Vídeo: Spotify pausado",  // StatusMediaPaused
    L"Spotify não encontrado",  // StatusNoSpotify
    L"Desativado",  // StatusDisabled
    L"Ativado",  // MenuEnabled
    L"Configurações",  // MenuSettings
    L"Sair",  // MenuExit
    L"Ocultar da bandeja",  // MenuHideTray
    L"Configurações do AdMutify",  // SettingsTitle
    L"Anúncios silenciados",  // AdsMutedCaption
    L"Ligado",  // ToggleOn
    L"Desligado",  // ToggleOff
    L"Iniciar com o Windows",  // OptAutostart
    L"Não mostrar ícone na área de notificação ao iniciar com o Windows",  // OptHideTray
    L"Aplica-se quando o AdMutify inicia com o Windows. Abra o AdMutify novamente para mostrar o ícone.",  // OptHideTrayHint
    L"Durante anúncios",  // GroupAds
    L"Silenciar",  // ModeMute
    L"Reduzir volume",  // ModeLower
    L"Tocar sua própria música",  // ModeMusic
    L"%d%%",  // PercentValue
    L"Durante vídeos em outros aplicativos",  // MediaLabel
    L"Não fazer nada",  // MediaOff
    L"Reduzir volume do Spotify",  // MediaLower
    L"Silenciar Spotify",  // MediaMute
    L"Pausar Spotify",  // MediaPause
    L"Reage apenas ao som de navegadores e players de vídeo que dura alguns segundos. Sons curtos, jogos e chamadas são ignorados.",  // MediaHint
    L"Reduzir para",  // MediaPercentLabel
    L"Fonte",  // MusicSource
    L"Um arquivo",  // MusicFile
    L"Pasta, aleatória",  // MusicShuffle
    L"Pasta, em ordem",  // MusicOrder
    L"Toca sua própria música durante os anúncios. Quando o anúncio termina, a música pausa e continua do mesmo ponto no próximo anúncio.",  // MusicHint
    L"Nenhum arquivo escolhido",  // MusicNoFile
    L"Nenhuma pasta escolhida",  // MusicNoFolder
    L"Escolher",  // MusicPick
    L"Volume",  // MusicVolumeLabel
    L"Escolher um arquivo de música",  // MusicPickFileTitle
    L"Escolher uma pasta de música",  // MusicPickFolderTitle
    L"Arquivos de música",  // MusicFilterName
    L"Esta pasta não tem arquivos de música reproduzíveis (%s). Escolha outra pasta.",  // ErrMusicEmpty
    L"Idioma",  // LanguageLabel
    L"Sistema",  // LangSystem
    L"Redefinir configurações",  // ResetButton
    L"Clique de novo para redefinir",  // ResetConfirmClick
    L"Fechar",  // Close
    L"O arquivo de configurações veio de uma versão mais recente do AdMutify. Os valores padrão são usados e o arquivo não é alterado.",  // ErrSettingsNewer
    L"Não foi possível salvar as configurações. Verifique se é possível gravar na pasta.",  // ErrSettingsSave
    L"Não foi possível alterar a entrada de inicialização do Windows.",  // ErrAutostart
    L"Erro do Win32 %lu: %s",  // ErrTechnical
    L"AdMutify foi fechado inesperadamente",  // CrashTitle
    L"O volume do Spotify foi restaurado. Se isso continuar acontecendo, envie os detalhes ao desenvolvedor.",  // CrashBody
    L"Não foi possível restaurar o volume do Spotify. Verifique-o no mixer de volume do Windows. Se isso continuar acontecendo, envie os detalhes ao desenvolvedor.",  // CrashBodyNotRestored
    L"Copiar detalhes",  // CrashCopy
    L"Abrir pasta de logs",  // CrashOpenLogs
};
static_assert(std::size(kPt) == kCount, "Brazilian Portuguese table size mismatch");

constexpr const wchar_t* kIt[] = {  // Italian
    L"Attivo: nessuna pubblicità",  // StatusActive
    L"Pubblicità: audio disattivato",  // StatusMuted
    L"Pubblicità: volume ridotto (%d%%)",  // StatusLowered
    L"Video: audio disattivato",  // StatusMediaMuted
    L"Video: volume ridotto (%d%%)",  // StatusMediaLowered
    L"Video: Spotify in pausa",  // StatusMediaPaused
    L"Spotify non trovato",  // StatusNoSpotify
    L"Disattivato",  // StatusDisabled
    L"Attivato",  // MenuEnabled
    L"Impostazioni",  // MenuSettings
    L"Esci",  // MenuExit
    L"Nascondi dall'area di notifica",  // MenuHideTray
    L"Impostazioni di AdMutify",  // SettingsTitle
    L"Pubblicità silenziate",  // AdsMutedCaption
    L"Acceso",  // ToggleOn
    L"Spento",  // ToggleOff
    L"Avvia con Windows",  // OptAutostart
    L"Nessuna icona nell'area di notifica all'avvio con Windows",  // OptHideTray
    L"Si applica quando AdMutify si avvia con Windows. Apri di nuovo AdMutify per visualizzare l'icona.",  // OptHideTrayHint
    L"Durante la pubblicità",  // GroupAds
    L"Disattiva audio",  // ModeMute
    L"Riduci volume",  // ModeLower
    L"Riproduci la tua musica",  // ModeMusic
    L"%d%%",  // PercentValue
    L"Durante i video in altre app",  // MediaLabel
    L"Nessuna azione",  // MediaOff
    L"Riduci volume di Spotify",  // MediaLower
    L"Disattiva audio di Spotify",  // MediaMute
    L"Metti Spotify in pausa",  // MediaPause
    L"Reagisce solo all'audio di browser e lettori video che dura alcuni secondi. I suoni brevi, i giochi e le chiamate vengono ignorati.",  // MediaHint
    L"Riduci a",  // MediaPercentLabel
    L"Origine",  // MusicSource
    L"Un file",  // MusicFile
    L"Cartella, casuale",  // MusicShuffle
    L"Cartella, in ordine",  // MusicOrder
    L"Riproduce la tua musica durante la pubblicità. Quando la pubblicità finisce, la musica va in pausa e riprende dallo stesso punto alla pubblicità successiva.",  // MusicHint
    L"Nessun file scelto",  // MusicNoFile
    L"Nessuna cartella scelta",  // MusicNoFolder
    L"Scegli",  // MusicPick
    L"Volume",  // MusicVolumeLabel
    L"Scegli un file musicale",  // MusicPickFileTitle
    L"Scegli una cartella musicale",  // MusicPickFolderTitle
    L"File musicali",  // MusicFilterName
    L"Questa cartella non contiene file musicali riproducibili (%s). Scegli un'altra cartella.",  // ErrMusicEmpty
    L"Lingua",  // LanguageLabel
    L"Sistema",  // LangSystem
    L"Reimposta impostazioni",  // ResetButton
    L"Fai clic di nuovo per ripristinare",  // ResetConfirmClick
    L"Chiudi",  // Close
    L"Il file delle impostazioni proviene da una versione più recente di AdMutify. Vengono usati i valori predefiniti e il file non viene modificato.",  // ErrSettingsNewer
    L"Non è stato possibile salvare le impostazioni. Verifica che sia possibile scrivere nella cartella.",  // ErrSettingsSave
    L"Non è stato possibile modificare la voce di avvio di Windows.",  // ErrAutostart
    L"Errore Win32 %lu: %s",  // ErrTechnical
    L"AdMutify si è chiuso in modo imprevisto",  // CrashTitle
    L"Il volume di Spotify è stato ripristinato. Se il problema persiste, invia i dettagli allo sviluppatore.",  // CrashBody
    L"Non è stato possibile ripristinare il volume di Spotify. Controllalo nel mixer volume di Windows. Se il problema persiste, invia i dettagli allo sviluppatore.",  // CrashBodyNotRestored
    L"Copia dettagli",  // CrashCopy
    L"Apri cartella dei log",  // CrashOpenLogs
};
static_assert(std::size(kIt) == kCount, "Italian table size mismatch");

constexpr const wchar_t* kNl[] = {  // Dutch
    L"Actief: geen advertentie",  // StatusActive
    L"Advertentie: gedempt",  // StatusMuted
    L"Advertentie: volume verlaagd (%d%%)",  // StatusLowered
    L"Video: gedempt",  // StatusMediaMuted
    L"Video: volume verlaagd (%d%%)",  // StatusMediaLowered
    L"Video: Spotify gepauzeerd",  // StatusMediaPaused
    L"Spotify niet gevonden",  // StatusNoSpotify
    L"Uitgeschakeld",  // StatusDisabled
    L"Ingeschakeld",  // MenuEnabled
    L"Instellingen",  // MenuSettings
    L"Afsluiten",  // MenuExit
    L"Verbergen uit systeemvak",  // MenuHideTray
    L"Instellingen voor AdMutify",  // SettingsTitle
    L"Advertenties gedempt",  // AdsMutedCaption
    L"Aan",  // ToggleOn
    L"Uit",  // ToggleOff
    L"Opstarten met Windows",  // OptAutostart
    L"Geen pictogram in het systeemvak bij opstarten met Windows",  // OptHideTray
    L"Geldt wanneer AdMutify met Windows wordt opgestart. Open AdMutify opnieuw om het pictogram weer te geven.",  // OptHideTrayHint
    L"Tijdens advertenties",  // GroupAds
    L"Dempen",  // ModeMute
    L"Volume verlagen",  // ModeLower
    L"Eigen muziek afspelen",  // ModeMusic
    L"%d%%",  // PercentValue
    L"Tijdens video's in andere apps",  // MediaLabel
    L"Niets doen",  // MediaOff
    L"Spotify zachter zetten",  // MediaLower
    L"Spotify dempen",  // MediaMute
    L"Spotify pauzeren",  // MediaPause
    L"Reageert alleen op geluid van browsers en videospelers dat enkele seconden aanhoudt. Korte geluiden, games en oproepen worden genegeerd.",  // MediaHint
    L"Verlagen tot",  // MediaPercentLabel
    L"Bron",  // MusicSource
    L"Eén bestand",  // MusicFile
    L"Map, willekeurig",  // MusicShuffle
    L"Map, op volgorde",  // MusicOrder
    L"Speelt je eigen muziek af tijdens advertenties. Als de advertentie eindigt, pauzeert de muziek en gaat bij de volgende advertentie verder op hetzelfde punt.",  // MusicHint
    L"Geen bestand gekozen",  // MusicNoFile
    L"Geen map gekozen",  // MusicNoFolder
    L"Kiezen",  // MusicPick
    L"Volume",  // MusicVolumeLabel
    L"Muziekbestand kiezen",  // MusicPickFileTitle
    L"Muziekmap kiezen",  // MusicPickFolderTitle
    L"Muziekbestanden",  // MusicFilterName
    L"Deze map bevat geen afspeelbare muziekbestanden (%s). Kies een andere map.",  // ErrMusicEmpty
    L"Taal",  // LanguageLabel
    L"Systeem",  // LangSystem
    L"Instellingen herstellen",  // ResetButton
    L"Klik nogmaals om terug te zetten",  // ResetConfirmClick
    L"Sluiten",  // Close
    L"Het instellingenbestand is afkomstig van een nieuwere versie van AdMutify. De standaardwaarden worden gebruikt en het bestand blijft ongewijzigd.",  // ErrSettingsNewer
    L"De instellingen kunnen niet worden opgeslagen. Controleer of naar de map kan worden geschreven.",  // ErrSettingsSave
    L"Het Windows-opstartitem kan niet worden gewijzigd.",  // ErrAutostart
    L"Win32-fout %lu: %s",  // ErrTechnical
    L"AdMutify is onverwacht afgesloten",  // CrashTitle
    L"Het volume van Spotify is hersteld. Als dit blijft gebeuren, stuur je de details naar de ontwikkelaar.",  // CrashBody
    L"Het volume van Spotify kon niet worden hersteld. Controleer het in de volumemixer van Windows. Als dit blijft gebeuren, stuur je de details naar de ontwikkelaar.",  // CrashBodyNotRestored
    L"Details kopiëren",  // CrashCopy
    L"Map met logboeken openen",  // CrashOpenLogs
};
static_assert(std::size(kNl) == kCount, "Dutch table size mismatch");

constexpr const wchar_t* kPl[] = {  // Polish
    L"Aktywny: brak reklam",  // StatusActive
    L"Reklama: wyciszona",  // StatusMuted
    L"Reklama: ściszono do %d %%",  // StatusLowered
    L"Wideo: wyciszone",  // StatusMediaMuted
    L"Wideo: ściszono do %d %%",  // StatusMediaLowered
    L"Wideo: Spotify wstrzymane",  // StatusMediaPaused
    L"Nie znaleziono Spotify",  // StatusNoSpotify
    L"Wyłączone",  // StatusDisabled
    L"Włączone",  // MenuEnabled
    L"Ustawienia",  // MenuSettings
    L"Zakończ",  // MenuExit
    L"Ukryj z zasobnika",  // MenuHideTray
    L"Ustawienia AdMutify",  // SettingsTitle
    L"Wyciszone reklamy",  // AdsMutedCaption
    L"Włączone",  // ToggleOn
    L"Wyłączone",  // ToggleOff
    L"Uruchamiaj z systemem Windows",  // OptAutostart
    L"Bez ikony w obszarze powiadomień po uruchomieniu z systemem Windows",  // OptHideTray
    L"Dotyczy uruchamiania AdMutify z systemem Windows. Aby wyświetlić ikonę, otwórz ponownie AdMutify.",  // OptHideTrayHint
    L"Podczas reklam",  // GroupAds
    L"Wycisz",  // ModeMute
    L"Zmniejsz głośność",  // ModeLower
    L"Odtwarzaj własną muzykę",  // ModeMusic
    L"%d %%",  // PercentValue
    L"Podczas wideo w innych aplikacjach",  // MediaLabel
    L"Nic nie rób",  // MediaOff
    L"Ścisz Spotify",  // MediaLower
    L"Wycisz Spotify",  // MediaMute
    L"Wstrzymaj Spotify",  // MediaPause
    L"Reaguje tylko na dźwięk z przeglądarek i odtwarzaczy wideo, który trwa kilka sekund. Krótkie dźwięki, gry i połączenia są ignorowane.",  // MediaHint
    L"Zmniejsz do",  // MediaPercentLabel
    L"Źródło",  // MusicSource
    L"Jeden plik",  // MusicFile
    L"Folder, losowo",  // MusicShuffle
    L"Folder, po kolei",  // MusicOrder
    L"Odtwarza Twoją muzykę podczas reklam. Gdy reklama się kończy, muzyka zatrzymuje się i przy następnej reklamie gra dalej od tego samego miejsca.",  // MusicHint
    L"Nie wybrano pliku",  // MusicNoFile
    L"Nie wybrano folderu",  // MusicNoFolder
    L"Wybierz",  // MusicPick
    L"Głośność",  // MusicVolumeLabel
    L"Wybierz plik muzyczny",  // MusicPickFileTitle
    L"Wybierz folder z muzyką",  // MusicPickFolderTitle
    L"Pliki muzyczne",  // MusicFilterName
    L"Ten folder nie zawiera plików muzycznych do odtworzenia (%s). Wybierz inny folder.",  // ErrMusicEmpty
    L"Język",  // LanguageLabel
    L"Systemowy",  // LangSystem
    L"Resetuj ustawienia",  // ResetButton
    L"Kliknij ponownie, aby zresetować",  // ResetConfirmClick
    L"Zamknij",  // Close
    L"Plik ustawień pochodzi z nowszej wersji AdMutify. Używane są wartości domyślne, a plik pozostaje niezmieniony.",  // ErrSettingsNewer
    L"Nie można zapisać ustawień. Sprawdź, czy można zapisywać w tym folderze.",  // ErrSettingsSave
    L"Nie można zmienić wpisu uruchamiania systemu Windows.",  // ErrAutostart
    L"Błąd Win32 %lu: %s",  // ErrTechnical
    L"AdMutify nieoczekiwanie zakończył działanie",  // CrashTitle
    L"Przywrócono głośność Spotify. Jeśli problem się powtarza, wyślij szczegóły deweloperowi.",  // CrashBody
    L"Nie udało się przywrócić głośności Spotify. Sprawdź ją w mikserze głośności systemu Windows. Jeśli problem się powtarza, wyślij szczegóły deweloperowi.",  // CrashBodyNotRestored
    L"Kopiuj szczegóły",  // CrashCopy
    L"Otwórz folder dzienników",  // CrashOpenLogs
};
static_assert(std::size(kPl) == kCount, "Polish table size mismatch");

constexpr const wchar_t* kTr[] = {  // Turkish
    L"Etkin: reklam oynatılmıyor",  // StatusActive
    L"Reklam: sesi kapatıldı",  // StatusMuted
    L"Reklam: ses azaltıldı (%%%d)",  // StatusLowered
    L"Video: sesi kapatıldı",  // StatusMediaMuted
    L"Video: ses azaltıldı (%%%d)",  // StatusMediaLowered
    L"Video: Spotify duraklatıldı",  // StatusMediaPaused
    L"Spotify bulunamadı",  // StatusNoSpotify
    L"Devre dışı",  // StatusDisabled
    L"Etkin",  // MenuEnabled
    L"Ayarlar",  // MenuSettings
    L"Çıkış",  // MenuExit
    L"Sistem tepsisinden gizle",  // MenuHideTray
    L"AdMutify Ayarları",  // SettingsTitle
    L"Sessize alınan reklamlar",  // AdsMutedCaption
    L"Açık",  // ToggleOn
    L"Kapalı",  // ToggleOff
    L"Windows ile başlat",  // OptAutostart
    L"Windows ile başlatıldığında bildirim alanı simgesini gösterme",  // OptHideTray
    L"AdMutify Windows ile başlatıldığında geçerlidir. Simgeyi göstermek için AdMutify'ı yeniden açın.",  // OptHideTrayHint
    L"Reklamlar sırasında",  // GroupAds
    L"Sesi kapat",  // ModeMute
    L"Ses düzeyini düşür",  // ModeLower
    L"Kendi müziğini çal",  // ModeMusic
    L"%%%d",  // PercentValue
    L"Diğer uygulamalardaki videolar sırasında",  // MediaLabel
    L"Hiçbir şey yapma",  // MediaOff
    L"Spotify'ın sesini azalt",  // MediaLower
    L"Spotify'ın sesini kapat",  // MediaMute
    L"Spotify'ı duraklat",  // MediaPause
    L"Yalnızca tarayıcılardan ve video oynatıcılardan gelen ve birkaç saniye süren sese tepki verir. Kısa sesler, oyunlar ve aramalar yok sayılır.",  // MediaHint
    L"Şuna düşür",  // MediaPercentLabel
    L"Kaynak",  // MusicSource
    L"Tek dosya",  // MusicFile
    L"Klasör, karışık",  // MusicShuffle
    L"Klasör, sırayla",  // MusicOrder
    L"Reklamlar sırasında kendi müziğini çalar. Reklam bitince müzik duraklar ve sonraki reklamda aynı yerden devam eder.",  // MusicHint
    L"Dosya seçilmedi",  // MusicNoFile
    L"Klasör seçilmedi",  // MusicNoFolder
    L"Seç",  // MusicPick
    L"Ses düzeyi",  // MusicVolumeLabel
    L"Müzik dosyası seç",  // MusicPickFileTitle
    L"Müzik klasörü seç",  // MusicPickFolderTitle
    L"Müzik dosyaları",  // MusicFilterName
    L"Bu klasörde çalınabilir müzik dosyası yok (%s). Başka bir klasör seç.",  // ErrMusicEmpty
    L"Dil",  // LanguageLabel
    L"Sistem",  // LangSystem
    L"Ayarları sıfırla",  // ResetButton
    L"Sıfırlamak için tekrar tıkla",  // ResetConfirmClick
    L"Kapat",  // Close
    L"Ayar dosyanız daha yeni bir AdMutify sürümünden geliyor. Varsayılan değerler kullanılır ve dosya değiştirilmez.",  // ErrSettingsNewer
    L"Ayarlar kaydedilemedi. Klasöre yazılabildiğini kontrol edin.",  // ErrSettingsSave
    L"Windows başlangıç girdisi değiştirilemedi.",  // ErrAutostart
    L"Win32 hatası %lu: %s",  // ErrTechnical
    L"AdMutify beklenmedik şekilde kapandı",  // CrashTitle
    L"Spotify'ın ses düzeyi geri yüklendi. Bu durum devam ederse ayrıntıları geliştiriciye gönderin.",  // CrashBody
    L"Spotify'ın ses düzeyi geri yüklenemedi. Windows ses karıştırıcısında kontrol edin. Bu durum devam ederse ayrıntıları geliştiriciye gönderin.",  // CrashBodyNotRestored
    L"Ayrıntıları kopyala",  // CrashCopy
    L"Günlük klasörünü aç",  // CrashOpenLogs
};
static_assert(std::size(kTr) == kCount, "Turkish table size mismatch");

constexpr const wchar_t* kJa[] = {  // Japanese
    L"有効: 広告なし",  // StatusActive
    L"広告: ミュート",  // StatusMuted
    L"広告: 音量を下げています (%d%%)",  // StatusLowered
    L"動画: ミュート",  // StatusMediaMuted
    L"動画: 音量を下げています (%d%%)",  // StatusMediaLowered
    L"動画: Spotify を一時停止中",  // StatusMediaPaused
    L"Spotify が見つかりません",  // StatusNoSpotify
    L"無効",  // StatusDisabled
    L"有効",  // MenuEnabled
    L"設定",  // MenuSettings
    L"終了",  // MenuExit
    L"トレイから隠す",  // MenuHideTray
    L"AdMutify の設定",  // SettingsTitle
    L"ミュートした広告",  // AdsMutedCaption
    L"オン",  // ToggleOn
    L"オフ",  // ToggleOff
    L"Windows と同時に起動",  // OptAutostart
    L"Windows と同時に起動するときに通知領域のアイコンを表示しない",  // OptHideTray
    L"AdMutify を Windows と同時に起動する場合に適用されます。アイコンを表示するには、AdMutify をもう一度開いてください。",  // OptHideTrayHint
    L"広告の再生中",  // GroupAds
    L"ミュート",  // ModeMute
    L"音量を下げる",  // ModeLower
    L"自分の音楽を再生",  // ModeMusic
    L"%d%%",  // PercentValue
    L"他のアプリでの動画再生中",  // MediaLabel
    L"何もしない",  // MediaOff
    L"Spotify の音量を下げる",  // MediaLower
    L"Spotify をミュート",  // MediaMute
    L"Spotify を一時停止",  // MediaPause
    L"ブラウザーや動画プレーヤーから数秒間続く音声だけに反応します。短い音、ゲーム、通話は無視されます。",  // MediaHint
    L"下げる音量",  // MediaPercentLabel
    L"ソース",  // MusicSource
    L"1 つのファイル",  // MusicFile
    L"フォルダー、シャッフル",  // MusicShuffle
    L"フォルダー、順番どおり",  // MusicOrder
    L"広告の再生中に自分の音楽を流します。広告が終わると音楽は一時停止し、次の広告で同じ位置から再開します。",  // MusicHint
    L"ファイルが選択されていません",  // MusicNoFile
    L"フォルダーが選択されていません",  // MusicNoFolder
    L"選択",  // MusicPick
    L"音量",  // MusicVolumeLabel
    L"音楽ファイルを選択",  // MusicPickFileTitle
    L"音楽フォルダーを選択",  // MusicPickFolderTitle
    L"音楽ファイル",  // MusicFilterName
    L"このフォルダーには再生できる音楽ファイル (%s) がありません。別のフォルダーを選択してください。",  // ErrMusicEmpty
    L"言語",  // LanguageLabel
    L"システム",  // LangSystem
    L"設定をリセット",  // ResetButton
    L"もう一度クリックしてリセット",  // ResetConfirmClick
    L"閉じる",  // Close
    L"設定ファイルは新しいバージョンの AdMutify で作成されています。既定値を使用し、ファイルは変更しません。",  // ErrSettingsNewer
    L"設定を保存できませんでした。フォルダーに書き込めることを確認してください。",  // ErrSettingsSave
    L"Windows のスタートアップ項目を変更できませんでした。",  // ErrAutostart
    L"Win32 エラー %lu: %s",  // ErrTechnical
    L"AdMutify が予期せず終了しました",  // CrashTitle
    L"Spotify の音量を元に戻しました。この問題が繰り返し発生する場合は、詳細を開発者に送信してください。",  // CrashBody
    L"Spotify の音量を元に戻せませんでした。Windows の音量ミキサーで確認してください。この問題が繰り返し発生する場合は、詳細を開発者に送信してください。",  // CrashBodyNotRestored
    L"詳細をコピー",  // CrashCopy
    L"ログ フォルダーを開く",  // CrashOpenLogs
};
static_assert(std::size(kJa) == kCount, "Japanese table size mismatch");

constexpr const wchar_t* kZh[] = {  // Simplified Chinese
    L"已启用：未播放广告",  // StatusActive
    L"广告：已静音",  // StatusMuted
    L"广告：音量已降低 (%d%%)",  // StatusLowered
    L"视频：已静音",  // StatusMediaMuted
    L"视频：音量已降低 (%d%%)",  // StatusMediaLowered
    L"视频：Spotify 已暂停",  // StatusMediaPaused
    L"未找到 Spotify",  // StatusNoSpotify
    L"已禁用",  // StatusDisabled
    L"已启用",  // MenuEnabled
    L"设置",  // MenuSettings
    L"退出",  // MenuExit
    L"从托盘隐藏",  // MenuHideTray
    L"AdMutify 设置",  // SettingsTitle
    L"已静音的广告",  // AdsMutedCaption
    L"开",  // ToggleOn
    L"关",  // ToggleOff
    L"随 Windows 启动",  // OptAutostart
    L"随 Windows 启动时不显示通知区域图标",  // OptHideTray
    L"适用于 AdMutify 随 Windows 启动的情况。要显示图标，请再次打开 AdMutify。",  // OptHideTrayHint
    L"播放广告时",  // GroupAds
    L"静音",  // ModeMute
    L"降低音量",  // ModeLower
    L"播放自己的音乐",  // ModeMusic
    L"%d%%",  // PercentValue
    L"在其他应用中播放视频时",  // MediaLabel
    L"不执行任何操作",  // MediaOff
    L"降低 Spotify 音量",  // MediaLower
    L"将 Spotify 静音",  // MediaMute
    L"暂停 Spotify",  // MediaPause
    L"仅响应浏览器和视频播放器中持续数秒的声音。短促声音、游戏和通话将被忽略。",  // MediaHint
    L"降至",  // MediaPercentLabel
    L"来源",  // MusicSource
    L"单个文件",  // MusicFile
    L"文件夹，随机",  // MusicShuffle
    L"文件夹，按顺序",  // MusicOrder
    L"在广告期间播放你自己的音乐。广告结束时音乐暂停，下一个广告时从同一位置继续播放。",  // MusicHint
    L"未选择文件",  // MusicNoFile
    L"未选择文件夹",  // MusicNoFolder
    L"选择",  // MusicPick
    L"音量",  // MusicVolumeLabel
    L"选择音乐文件",  // MusicPickFileTitle
    L"选择音乐文件夹",  // MusicPickFolderTitle
    L"音乐文件",  // MusicFilterName
    L"此文件夹中没有可播放的音乐文件（%s）。请选择其他文件夹。",  // ErrMusicEmpty
    L"语言",  // LanguageLabel
    L"系统",  // LangSystem
    L"重置设置",  // ResetButton
    L"再次单击以重置",  // ResetConfirmClick
    L"关闭",  // Close
    L"你的设置文件来自较新版本的 AdMutify。将使用默认值，并且不会更改该文件。",  // ErrSettingsNewer
    L"无法保存设置。请检查该文件夹是否可写。",  // ErrSettingsSave
    L"无法更改 Windows 启动项。",  // ErrAutostart
    L"Win32 错误 %lu：%s",  // ErrTechnical
    L"AdMutify 意外关闭",  // CrashTitle
    L"Spotify 音量已恢复。如果此问题持续发生，请将详细信息发送给开发者。",  // CrashBody
    L"无法恢复 Spotify 音量。请在 Windows 音量合成器中检查。如果此问题持续发生，请将详细信息发送给开发者。",  // CrashBodyNotRestored
    L"复制详细信息",  // CrashCopy
    L"打开日志文件夹",  // CrashOpenLogs
};
static_assert(std::size(kZh) == kCount, "Simplified Chinese table size mismatch");
// END GENERATED TABLES

Language g_language = Language::English;

const wchar_t* const* TableFor(Language language) {
    switch (language) {
        case Language::German: return kDe;
        case Language::Spanish: return kEs;
        case Language::Portuguese: return kPt;
        case Language::Italian: return kIt;
        case Language::Dutch: return kNl;
        case Language::Polish: return kPl;
        case Language::Turkish: return kTr;
        case Language::Japanese: return kJa;
        case Language::Chinese: return kZh;
        default: return kEn;
    }
}

}  // namespace

Language LanguageFromLangId(unsigned short langId) {
    switch (PRIMARYLANGID(langId)) {
        case LANG_GERMAN: return Language::German;
        case LANG_SPANISH: return Language::Spanish;
        case LANG_PORTUGUESE: return Language::Portuguese;
        case LANG_ITALIAN: return Language::Italian;
        case LANG_DUTCH: return Language::Dutch;
        case LANG_POLISH: return Language::Polish;
        case LANG_TURKISH: return Language::Turkish;
        case LANG_JAPANESE: return Language::Japanese;
        case LANG_CHINESE: return Language::Chinese;
        default: return Language::English;
    }
}

Language ResolveLanguage(Language language) {
    if (language == Language::System) return LanguageFromLangId(GetUserDefaultUILanguage());
    return language;
}

const wchar_t* LanguageNativeName(Language language) {
    switch (language) {
        case Language::English: return L"English";
        case Language::German: return L"Deutsch";
        case Language::Spanish: return L"Espa\u00F1ol";
        case Language::Portuguese: return L"Portugu\u00EAs (Brasil)";
        case Language::Italian: return L"Italiano";
        case Language::Dutch: return L"Nederlands";
        case Language::Polish: return L"Polski";
        case Language::Turkish: return L"T\u00FCrk\u00E7e";
        case Language::Japanese: return L"\u65E5\u672C\u8A9E";
        case Language::Chinese: return L"\u7B80\u4F53\u4E2D\u6587";
        default: return L"";
    }
}

void SetLanguage(Language language) { g_language = ResolveLanguage(language); }

const wchar_t* StrFor(Language language, StrId id) {
    size_t i = static_cast<size_t>(id);
    if (i >= kCount) return L"";
    return TableFor(ResolveLanguage(language))[i];
}

const wchar_t* Str(StrId id) { return StrFor(g_language, id); }

std::wstring FormatPercent(StrId id, int percent) {
    wchar_t buf[128];
    swprintf_s(buf, Str(id), percent);
    return buf;
}

std::wstring TooltipText(const wchar_t* status) {
    std::wstring t = L"AdMutify\n";
    t += status;
    if (t.size() > 127) {
        // Never end on a lone high surrogate: the shell would show a broken character.
        size_t keep = 127;
        if (t[keep - 1] >= 0xD800 && t[keep - 1] <= 0xDBFF) --keep;
        t.resize(keep);
    }
    return t;
}
