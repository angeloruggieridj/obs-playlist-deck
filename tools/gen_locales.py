#!/usr/bin/env python3
# Generates OBS locale .ini files for Playlist Deck from a translation table.
# en-US and it-IT are authored by hand; this fills the other shipped languages.
import os

KEYS = [
    "Section.MediaSource", "Section.Playlist", "Section.PlaylistFile",
    "Filter.Placeholder",
    "OnEnd.Label", "OnEnd.PlayNext", "OnEnd.Loop", "OnEnd.LoadNext",
    "OnEnd.Stop", "OnEnd.Shuffle", "OnEnd.RepeatOne",
    "Btn.Refresh", "Btn.Add", "Btn.Remove", "Btn.Up", "Btn.Down", "Btn.Clear",
    "Btn.Play", "Btn.Prev", "Btn.Pause", "Btn.Stop", "Btn.Next", "Btn.Save",
    "Btn.Open", "Btn.Settings",
    "Tip.Refresh", "Tip.Add", "Tip.Remove", "Tip.Up", "Tip.Down", "Tip.Clear",
    "Tip.Play", "Tip.Prev", "Tip.Pause", "Tip.Stop", "Tip.Next", "Tip.Save",
    "Tip.Open", "Tip.Settings", "Tip.Version",
    "NoPlaylist", "Loaded",
    "Settings.Title", "Settings.Probe", "Settings.AutoRestore",
    "Settings.Language", "Settings.Saved", "Lang.Auto",
]

T = {
 "es-ES": ["Fuente multimedia","Lista de reproducción","Archivo de lista","Filtrar…",
  "Al terminar:","Reproducir siguiente","Repetir todo","Cargar siguiente (en pausa)","Detener","Aleatorio","Repetir uno",
  "Actualizar","Añadir","Quitar","Subir","Bajar","Vaciar","Reproducir","Ant.","Pausa","Detener","Sig.","Guardar","Abrir","Ajustes",
  "Volver a buscar fuentes multimedia","Añadir archivos multimedia","Quitar el elemento seleccionado","Subir el elemento seleccionado","Bajar el elemento seleccionado","Vaciar la lista","Reproducir el elemento seleccionado","Elemento anterior","Reproducir / pausar la fuente vinculada","Detener la reproducción","Elemento siguiente","Guardar la lista en un archivo que elijas","Abrir un archivo de lista (.json / .m3u)","Abrir los ajustes de Playlist Deck","Versión instalada de Playlist Deck",
  "Ninguna lista cargada","Cargada: %1",
  "Playlist Deck — Ajustes","Detectar duración de los medios (en segundo plano)","Restaurar la última lista al iniciar","Idioma","Ajustes guardados.","Automático (seguir OBS)"],

 "fr-FR": ["Source multimédia","Liste de lecture","Fichier de liste","Filtrer…",
  "À la fin :","Lire la suivante","Boucle","Charger la suivante (en pause)","Arrêter","Aléatoire","Répéter un",
  "Actualiser","Ajouter","Retirer","Monter","Descendre","Vider","Lire","Préc.","Pause","Arrêter","Suiv.","Enregistrer","Ouvrir","Réglages",
  "Rechercher à nouveau les sources","Ajouter des fichiers multimédias","Retirer l'élément sélectionné","Monter l'élément sélectionné","Descendre l'élément sélectionné","Vider la liste","Lire l'élément sélectionné","Élément précédent","Lecture / pause de la source liée","Arrêter la lecture","Élément suivant","Enregistrer la liste dans un fichier de votre choix","Ouvrir un fichier de liste (.json / .m3u)","Ouvrir les réglages de Playlist Deck","Version installée de Playlist Deck",
  "Aucune liste chargée","Chargée : %1",
  "Playlist Deck — Réglages","Détecter la durée des médias (en arrière-plan)","Restaurer la dernière liste au démarrage","Langue","Réglages enregistrés.","Auto (suivre OBS)"],

 "de-DE": ["Medienquelle","Wiedergabeliste","Wiedergabelisten-Datei","Filtern…",
  "Am Ende:","Nächste abspielen","Schleife","Nächste laden (pausiert)","Stopp","Zufall","Eine wiederholen",
  "Aktualisieren","Hinzufügen","Entfernen","Hoch","Runter","Leeren","Abspielen","Zurück","Pause","Stopp","Weiter","Speichern","Öffnen","Einstellungen",
  "Medienquellen neu einlesen","Mediendateien hinzufügen","Ausgewähltes Element entfernen","Ausgewähltes Element nach oben","Ausgewähltes Element nach unten","Wiedergabeliste leeren","Ausgewähltes Element abspielen","Vorheriges Element","Gebundene Quelle abspielen / pausieren","Wiedergabe stoppen","Nächstes Element","Wiedergabeliste in eine Datei deiner Wahl speichern","Wiedergabelisten-Datei öffnen (.json / .m3u)","Playlist-Deck-Einstellungen öffnen","Installierte Playlist-Deck-Version",
  "Keine Wiedergabeliste geladen","Geladen: %1",
  "Playlist Deck — Einstellungen","Mediendauer ermitteln (im Hintergrund)","Letzte Wiedergabeliste beim Start wiederherstellen","Sprache","Einstellungen gespeichert.","Automatisch (OBS folgen)"],

 "pt-BR": ["Fonte de mídia","Playlist","Arquivo de playlist","Filtrar…",
  "Ao terminar:","Tocar próxima","Repetir tudo","Carregar próxima (pausada)","Parar","Aleatório","Repetir uma",
  "Atualizar","Adicionar","Remover","Subir","Descer","Limpar","Tocar","Ant.","Pausar","Parar","Próx.","Salvar","Abrir","Configurações",
  "Reescanear fontes de mídia","Adicionar arquivos de mídia","Remover o item selecionado","Subir o item selecionado","Descer o item selecionado","Limpar a playlist","Tocar o item selecionado","Item anterior","Tocar / pausar a fonte vinculada","Parar a reprodução","Próximo item","Salvar a playlist em um arquivo à sua escolha","Abrir um arquivo de playlist (.json / .m3u)","Abrir as configurações do Playlist Deck","Versão instalada do Playlist Deck",
  "Nenhuma playlist carregada","Carregada: %1",
  "Playlist Deck — Configurações","Detectar duração das mídias (em segundo plano)","Restaurar a última playlist ao iniciar","Idioma","Configurações salvas.","Automático (seguir o OBS)"],

 "ru-RU": ["Медиаисточник","Плейлист","Файл плейлиста","Фильтр…",
  "По окончании:","Следующий","Повтор всего","Загрузить следующий (пауза)","Стоп","Случайно","Повтор одного",
  "Обновить","Добавить","Удалить","Вверх","Вниз","Очистить","Воспр.","Пред.","Пауза","Стоп","След.","Сохранить","Открыть","Настройки",
  "Пересканировать медиаисточники","Добавить медиафайлы","Удалить выбранный элемент","Переместить вверх","Переместить вниз","Очистить плейлист","Воспроизвести выбранный элемент","Предыдущий элемент","Воспр./пауза привязанного источника","Остановить воспроизведение","Следующий элемент","Сохранить плейлист в выбранный файл","Открыть файл плейлиста (.json / .m3u)","Открыть настройки Playlist Deck","Установленная версия Playlist Deck",
  "Плейлист не загружен","Загружен: %1",
  "Playlist Deck — Настройки","Определять длительность медиа (в фоне)","Восстанавливать последний плейлист при запуске","Язык","Настройки сохранены.","Авто (как в OBS)"],

 "zh-CN": ["媒体源","播放列表","播放列表文件","筛选…",
  "结束时：","播放下一个","循环","加载下一个（暂停）","停止","随机","单曲循环",
  "刷新","添加","移除","上移","下移","清空","播放","上一个","暂停","停止","下一个","保存","打开","设置",
  "重新扫描媒体源","添加媒体文件","移除所选项","上移所选项","下移所选项","清空播放列表","播放所选项","上一项","播放/暂停绑定的源","停止播放","下一项","将播放列表保存到所选文件","打开播放列表文件（.json / .m3u）","打开 Playlist Deck 设置","已安装的 Playlist Deck 版本",
  "未加载播放列表","已加载：%1",
  "Playlist Deck — 设置","在后台检测媒体时长","启动时恢复上次的播放列表","语言","设置已保存。","自动（跟随 OBS）"],

 "ja-JP": ["メディアソース","プレイリスト","プレイリストファイル","フィルター…",
  "終了時:","次を再生","ループ","次を読み込む（一時停止）","停止","シャッフル","1曲リピート",
  "更新","追加","削除","上へ","下へ","クリア","再生","前へ","一時停止","停止","次へ","保存","開く","設定",
  "メディアソースを再スキャン","メディアファイルを追加","選択項目を削除","選択項目を上へ","選択項目を下へ","プレイリストをクリア","選択項目を再生","前の項目","バインド先ソースの再生/一時停止","再生を停止","次の項目","プレイリストを任意のファイルに保存","プレイリストファイルを開く（.json / .m3u）","Playlist Deck の設定を開く","インストール済みの Playlist Deck バージョン",
  "プレイリスト未読み込み","読み込み済み: %1",
  "Playlist Deck — 設定","メディアの長さをバックグラウンドで取得","起動時に前回のプレイリストを復元","言語","設定を保存しました。","自動（OBSに従う）"],

 "ko-KR": ["미디어 소스","재생목록","재생목록 파일","필터…",
  "끝나면:","다음 재생","반복","다음 불러오기 (일시정지)","정지","셔플","한 곡 반복",
  "새로고침","추가","제거","위로","아래로","비우기","재생","이전","일시정지","정지","다음","저장","열기","설정",
  "미디어 소스 다시 검색","미디어 파일 추가","선택 항목 제거","선택 항목 위로","선택 항목 아래로","재생목록 비우기","선택 항목 재생","이전 항목","연결된 소스 재생/일시정지","재생 정지","다음 항목","재생목록을 원하는 파일로 저장","재생목록 파일 열기 (.json / .m3u)","Playlist Deck 설정 열기","설치된 Playlist Deck 버전",
  "재생목록 없음","불러옴: %1",
  "Playlist Deck — 설정","미디어 길이 백그라운드 감지","시작 시 마지막 재생목록 복원","언어","설정이 저장되었습니다.","자동 (OBS 따름)"],
}

out_dir = os.path.join(os.path.dirname(__file__), "..", "data", "locale")
for code, vals in T.items():
    assert len(vals) == len(KEYS), f"{code}: {len(vals)} != {len(KEYS)}"
    lines = ['PlaylistDeck="Playlist Deck"', ""]
    for k, v in zip(KEYS, vals):
        lines.append(f'{k}="{v}"')
    path = os.path.join(out_dir, code + ".ini")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", code, len(vals), "keys")
