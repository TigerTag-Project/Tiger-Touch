/* Every sentence the screens draw, in the 11 languages of Tiger Studio Manager.
 *
 * Columns, in Studio's own order:
 *   en, fr, de, es, it, zh, pt (BR), pt-pt, pl, ru, nl
 *
 * Rules (they are Studio's, see its CLAUDE.md):
 *  - all 11 columns are written in the same edit; none is left empty, and an
 *    empty one falls back to English at run time;
 *  - the translations start from the English column, with the vocabulary of
 *    Studio's own locale files, never from the French;
 *  - one format per row: the same %d / %s in the same order in every column,
 *    and no sentence that needs a plural.
 *
 * Widths matter: the panel is 800 px and a few of these sit in a tile or a
 * chip. The ones that do are kept short on purpose.
 *
 * Adding a row: add it here, then `python tools/make_fonts.py` if it brings a
 * character the faces do not hold (Chinese or a new symbol). */

/* ---- Start-up and loading --------------------------------------------- */
X(S_BUSY_WIFI, "Connecting to Wi-Fi", "Connexion au Wi-Fi", "Verbinde mit WLAN", "Conectando al Wi-Fi",
  "Connessione al Wi-Fi", "正在连接 Wi-Fi", "Conectando ao Wi-Fi", "A ligar ao Wi-Fi", "Łączenie z Wi-Fi",
  "Подключение к Wi-Fi", "Verbinden met wifi")
X(S_BUSY_SCAN, "Searching for networks", "Recherche des réseaux", "Suche nach Netzwerken", "Buscando redes",
  "Ricerca delle reti", "正在搜索网络", "Procurando redes", "A procurar redes", "Wyszukiwanie sieci",
  "Поиск сетей", "Netwerken zoeken")
X(S_BUSY_CONNECT_TO, "Connecting to", "Connexion à", "Verbinde mit", "Conectando a", "Connessione a",
  "正在连接", "Conectando a", "A ligar a", "Łączenie z", "Подключение к", "Verbinden met")
X(S_BUSY_TIME, "Setting the clock", "Mise à l'heure", "Uhrzeit wird gestellt", "Ajustando la hora",
  "Impostazione dell'ora", "正在校准时间", "Ajustando a hora", "A acertar a hora", "Ustawianie zegara",
  "Установка времени", "Klok instellen")
X(S_BUSY_TIME_WHY, "Needed for secure connections", "Nécessaire pour les connexions sécurisées",
  "Für sichere Verbindungen nötig", "Necesario para las conexiones seguras",
  "Necessario per le connessioni sicure", "安全连接需要此步骤", "Necessário para conexões seguras",
  "Necessário para ligações seguras", "Wymagane dla bezpiecznych połączeń",
  "Нужно для безопасных соединений", "Nodig voor beveiligde verbindingen")
X(S_BUSY_ACCOUNT, "Signing in", "Connexion au compte", "Anmeldung läuft", "Iniciando sesión",
  "Accesso in corso", "正在登录", "Entrando", "A iniciar sessão", "Logowanie", "Вход в аккаунт", "Inloggen")
X(S_BUSY_PRINTERS, "Reading your printers", "Lecture des imprimantes", "Drucker werden gelesen",
  "Leyendo las impresoras", "Lettura delle stampanti", "正在读取打印机", "Lendo as impressoras",
  "A ler as impressoras", "Odczyt drukarek", "Чтение принтеров", "Printers ophalen")
X(S_BUSY_LOADING, "Loading", "Chargement", "Wird geladen", "Cargando", "Caricamento", "加载中",
  "Carregando", "A carregar", "Ładowanie", "Загрузка", "Laden")
X(S_BUSY_CONNECT_PRINTER, "Connecting to %s", "Connexion à %s", "Verbinde mit %s", "Conectando a %s",
  "Connessione a %s", "正在连接 %s", "Conectando a %s", "A ligar a %s", "Łączenie z %s",
  "Подключение к %s", "Verbinden met %s")

/* ---- Wi-Fi and account forms ------------------------------------------ */
X(S_WIFI_PASSWORD, "Wi-Fi password", "Mot de passe Wi-Fi", "WLAN-Passwort", "Contraseña del Wi-Fi",
  "Password Wi-Fi", "Wi-Fi 密码", "Senha do Wi-Fi", "Palavra-passe do Wi-Fi", "Hasło Wi-Fi",
  "Пароль Wi-Fi", "Wifi-wachtwoord")
X(S_PASSWORD, "Password", "Mot de passe", "Passwort", "Contraseña", "Password", "密码", "Senha",
  "Palavra-passe", "Hasło", "Пароль", "Wachtwoord")
X(S_EMAIL_ADDRESS, "Email address", "Adresse e-mail", "E-Mail-Adresse", "Correo electrónico",
  "Indirizzo email", "邮箱地址", "Endereço de e-mail", "Endereço de e-mail", "Adres e-mail", "Email",
  "E-mailadres")
X(S_EMAIL, "Email", "E-mail", "E-Mail", "Correo electrónico", "Email", "邮箱", "E-mail", "E-mail",
  "Email", "Email", "E-mail")
X(S_SAME_ACCOUNT, "The same account as in Tiger Studio", "Le même compte que dans Tiger Studio",
  "Dasselbe Konto wie in Tiger Studio", "La misma cuenta que en Tiger Studio",
  "Lo stesso account di Tiger Studio", "与 Tiger Studio 相同的账号", "A mesma conta do Tiger Studio",
  "A mesma conta do Tiger Studio", "To samo konto co w Tiger Studio",
  "Тот же аккаунт, что и в Tiger Studio", "Hetzelfde account als in Tiger Studio")
X(S_ERR_NET_MISSING, "The saved network can't be found.", "Le réseau enregistré est introuvable.",
  "Das gespeicherte Netzwerk wurde nicht gefunden.", "No se encuentra la red guardada.",
  "La rete salvata non è stata trovata.", "找不到已保存的网络。", "A rede salva não foi encontrada.",
  "A rede guardada não foi encontrada.", "Nie znaleziono zapisanej sieci.", "Сохранённая сеть не найдена.",
  "Het opgeslagen netwerk is niet gevonden.")
X(S_ERR_WIFI_PASSWORD, "Can't connect. Check the password.", "Connexion impossible : vérifie le mot de passe.",
  "Verbindung nicht möglich. Prüfe das Passwort.", "No se pudo conectar. Revisa la contraseña.",
  "Connessione non riuscita. Controlla la password.", "无法连接，请检查密码。",
  "Não foi possível conectar. Confira a senha.", "Não foi possível ligar. Verifique a palavra-passe.",
  "Nie można się połączyć. Sprawdź hasło.", "Не удалось подключиться. Проверьте пароль.",
  "Verbinden lukt niet. Controleer het wachtwoord.")
X(S_ERR_WIFI_FAIL, "Can't connect to this network.", "Connexion impossible à ce réseau.",
  "Verbindung mit diesem Netzwerk nicht möglich.", "No se pudo conectar a esta red.",
  "Impossibile connettersi a questa rete.", "无法连接到此网络。",
  "Não foi possível conectar a esta rede.", "Não foi possível ligar a esta rede.",
  "Nie można połączyć się z tą siecią.", "Не удалось подключиться к этой сети.",
  "Verbinden met dit netwerk lukt niet.")
X(S_ERR_NEED_EMAIL, "Enter your email address.", "Renseigne l'adresse e-mail.",
  "Gib deine E-Mail-Adresse ein.", "Introduce tu correo electrónico.", "Inserisci il tuo indirizzo email.",
  "请输入邮箱地址。", "Digite seu e-mail.", "Introduza o seu e-mail.", "Wpisz adres e-mail.",
  "Введите адрес email.", "Vul je e-mailadres in.")
X(S_ERR_NEED_PASSWORD, "Enter your password.", "Renseigne le mot de passe.", "Gib dein Passwort ein.",
  "Introduce tu contraseña.", "Inserisci la tua password.", "请输入密码。", "Digite sua senha.",
  "Introduza a sua palavra-passe.", "Wpisz hasło.", "Введите пароль.", "Vul je wachtwoord in.")
X(S_ERR_ACCOUNT_SILENT, "The account isn't answering. Check your Wi-Fi.",
  "Le compte ne répond pas. Vérifie la connexion Wi-Fi.", "Das Konto antwortet nicht. Prüfe dein WLAN.",
  "La cuenta no responde. Revisa tu Wi-Fi.", "L'account non risponde. Controlla il Wi-Fi.",
  "账号无响应，请检查 Wi-Fi。", "A conta não responde. Confira o Wi-Fi.",
  "A conta não responde. Verifique o Wi-Fi.", "Konto nie odpowiada. Sprawdź Wi-Fi.",
  "Аккаунт не отвечает. Проверьте Wi-Fi.", "Het account antwoordt niet. Controleer je wifi.")
X(S_ERR_CREDENTIALS, "Wrong email or password.", "E-mail ou mot de passe incorrect.",
  "E-Mail oder Passwort falsch.", "Correo o contraseña incorrectos.", "Email o password errati.",
  "邮箱或密码错误。", "E-mail ou senha incorretos.", "E-mail ou palavra-passe incorretos.",
  "Nieprawidłowy e-mail lub hasło.", "Неверный email или пароль.",
  "E-mailadres of wachtwoord onjuist.")
X(S_ERR_GOOGLE, "Google account: use Continue with Google.",
  "Compte Google : utilisez Continuer avec Google.", "Google-Konto: Weiter mit Google verwenden.",
  "Cuenta de Google: use Continuar con Google.", "Account Google: usa Continua con Google.",
  "Google 账号：请使用 Google 继续。", "Conta Google: use Continuar com o Google.",
  "Conta Google: use Continuar com o Google.", "Konto Google: użyj Kontynuuj z Google.",
  "Аккаунт Google: нажмите «Продолжить с Google».", "Google-account: gebruik Doorgaan met Google.")
X(S_ERR_RATE, "Too many attempts. Try again in a few minutes.", "Trop d'essais. Réessaie dans quelques minutes.",
  "Zu viele Versuche. Versuche es in ein paar Minuten erneut.",
  "Demasiados intentos. Inténtalo de nuevo en unos minutos.",
  "Troppi tentativi. Riprova tra qualche minuto.", "尝试次数过多，请几分钟后再试。",
  "Muitas tentativas. Tente novamente em alguns minutos.",
  "Demasiadas tentativas. Tente novamente dentro de alguns minutos.",
  "Zbyt wiele prób. Spróbuj ponownie za kilka minut.", "Слишком много попыток. Повторите через несколько минут.",
  "Te veel pogingen. Probeer het over een paar minuten opnieuw.")
X(S_ERR_DISABLED, "This account is disabled.", "Ce compte est désactivé.", "Dieses Konto ist deaktiviert.",
  "Esta cuenta está desactivada.", "Questo account è disattivato.", "此账号已被停用。",
  "Esta conta está desativada.", "Esta conta está desativada.", "To konto jest wyłączone.",
  "Этот аккаунт отключён.", "Dit account is uitgeschakeld.")
X(S_ERR_OTHER, "Can't sign in.", "Connexion impossible.", "Anmeldung nicht möglich.",
  "No se pudo iniciar sesión.", "Accesso non riuscito.", "无法登录。", "Não foi possível entrar.",
  "Não foi possível iniciar sessão.", "Nie można się zalogować.", "Не удалось войти.", "Inloggen lukt niet.")

/* ---- Networks and generic buttons ------------------------------------- */
X(S_NO_NETWORKS, "No network found.", "Aucun réseau trouvé.", "Kein Netzwerk gefunden.",
  "No se encontró ninguna red.", "Nessuna rete trovata.", "未找到网络。", "Nenhuma rede encontrada.",
  "Nenhuma rede encontrada.", "Nie znaleziono sieci.", "Сети не найдены.", "Geen netwerk gevonden.")
X(S_CANCEL, "Cancel", "Annuler", "Abbrechen", "Cancelar", "Annulla", "取消", "Cancelar", "Cancelar",
  "Anuluj", "Отмена", "Annuleren")
X(S_CONFIRM, "Confirm", "Valider", "Bestätigen", "Confirmar", "Conferma", "确认", "Confirmar", "Confirmar",
  "Potwierdź", "Подтвердить", "Bevestigen")
X(S_ACCOUNT_LINKED, "Account linked", "Compte lié", "Konto verknüpft", "Cuenta vinculada",
  "Account collegato", "账号已关联", "Conta vinculada", "Conta associada", "Konto połączone",
  "Аккаунт привязан", "Account gekoppeld")

/* ---- Settings --------------------------------------------------------- */
X(S_SETTINGS, "Settings", "Paramètres", "Einstellungen", "Ajustes", "Impostazioni", "设置",
  "Configurações", "Definições", "Ustawienia", "Настройки", "Instellingen")
X(S_PRINTERS, "Printers", "Imprimantes", "Drucker", "Impresoras", "Stampanti", "打印机", "Impressoras",
  "Impressoras", "Drukarki", "Принтеры", "Printers")
X(S_WIFI, "Wi-Fi", "Wi-Fi", "WLAN", "Wi-Fi", "Wi-Fi", "Wi-Fi", "Wi-Fi", "Wi-Fi", "Wi-Fi", "Wi-Fi", "Wifi")
X(S_SCREEN, "Screen", "Écran", "Bildschirm", "Pantalla", "Schermo", "屏幕", "Tela", "Ecrã", "Ekran",
  "Экран", "Scherm")
X(S_LANGUAGE, "Language", "Langue", "Sprache", "Idioma", "Lingua", "语言", "Idioma", "Idioma", "Język",
  "Язык", "Taal")
X(S_ACCOUNT, "Account", "Compte", "Konto", "Cuenta", "Account", "账号", "Conta", "Conta", "Konto", "Аккаунт",
  "Account")
X(S_RESTART, "Restart", "Redémarrer", "Neu starten", "Reiniciar", "Riavvia", "重启", "Reiniciar",
  "Reiniciar", "Uruchom ponownie", "Перезапустить", "Herstarten")
X(S_RESTART_ASK, "Restart the Tiger Touch?", "Redémarrer le Tiger Touch ?", "Tiger Touch neu starten?",
  "¿Reiniciar el Tiger Touch?", "Riavviare il Tiger Touch?", "要重启 Tiger Touch 吗？",
  "Reiniciar o Tiger Touch?", "Reiniciar o Tiger Touch?", "Uruchomić ponownie Tiger Touch?",
  "Перезапустить Tiger Touch?", "Tiger Touch herstarten?")
X(S_RESTART_WHY, "Printers will reconnect on their own.", "Les imprimantes se reconnecteront d'elles-mêmes.",
  "Die Drucker verbinden sich von selbst wieder.", "Las impresoras se reconectarán solas.",
  "Le stampanti si ricollegheranno da sole.", "打印机会自动重新连接。",
  "As impressoras vão se reconectar sozinhas.", "As impressoras voltam a ligar-se sozinhas.",
  "Drukarki połączą się ponownie same.", "Принтеры подключатся снова сами.",
  "De printers verbinden zich vanzelf opnieuw.")
X(S_FACTORY, "Factory reset", "Réinitialisation usine", "Werksreset", "Restablecer de fábrica",
  "Ripristino di fabbrica", "恢复出厂设置", "Restaurar de fábrica", "Repor de fábrica", "Reset fabryczny",
  "Сброс к заводским настройкам", "Fabrieksreset")
X(S_FACTORY_ASK, "Erase everything?", "Tout effacer ?", "Alles löschen?", "¿Borrar todo?",
  "Cancellare tutto?", "要清除所有数据吗？", "Apagar tudo?", "Apagar tudo?", "Usunąć wszystko?",
  "Стереть всё?", "Alles wissen?")
X(S_FACTORY_WHY, "Wi-Fi, account, language and settings will be lost. The Tiger Touch restarts as new.",
  "Le Wi-Fi, le compte, la langue et les réglages seront perdus. Le Tiger Touch redémarre comme neuf.",
  "WLAN, Konto, Sprache und Einstellungen gehen verloren. Tiger Touch startet wie neu.",
  "Se perderán el Wi-Fi, la cuenta, el idioma y los ajustes. El Tiger Touch se reinicia como nuevo.",
  "Wi-Fi, account, lingua e impostazioni andranno persi. Il Tiger Touch si riavvia come nuovo.",
  "Wi-Fi、账号、语言和设置将丢失。Tiger Touch 将重启并恢复为全新状态。",
  "Wi-Fi, conta, idioma e configurações serão perdidos. O Tiger Touch reinicia como novo.",
  "Wi-Fi, conta, idioma e definições serão perdidos. O Tiger Touch reinicia como novo.",
  "Wi-Fi, konto, język i ustawienia zostaną utracone. Tiger Touch uruchomi się jak nowy.",
  "Wi-Fi, аккаунт, язык и настройки будут удалены. Tiger Touch перезапустится как новый.",
  "Wifi, account, taal en instellingen gaan verloren. Tiger Touch start opnieuw op als nieuw.")
X(S_FACTORY_DO, "Erase", "Effacer", "Löschen", "Borrar", "Cancella", "清除", "Apagar", "Apagar", "Usuń",
  "Стереть", "Wissen")

/* ---- First-boot setup (as on TigerScale V3) --------------------------- */
X(S_OB_WIFI_Q, "Connect to Wi-Fi?", "Connecter au Wi-Fi ?", "Mit WLAN verbinden?", "¿Conectar al Wi-Fi?",
  "Connettere al Wi-Fi?", "连接 Wi-Fi？", "Conectar ao Wi-Fi?", "Ligar ao Wi-Fi?", "Połączyć z Wi-Fi?",
  "Подключиться к Wi-Fi?", "Verbinden met wifi?")
X(S_OB_WIFI_SUB, "Needed to link an account and see your printers",
  "Nécessaire pour lier un compte et voir vos imprimantes", "Nötig für das Konto und Ihre Drucker",
  "Necesario para vincular una cuenta y ver sus impresoras",
  "Necessario per collegare un account e vedere le stampanti", "绑定账户和查看打印机需要网络",
  "Necessário para vincular uma conta e ver suas impressoras",
  "Necessário para ligar uma conta e ver as suas impressoras",
  "Wymagane do połączenia konta i podglądu drukarek", "Нужно для привязки аккаунта и просмотра принтеров",
  "Nodig om een account te koppelen en je printers te zien")
X(S_OB_ACCOUNT_Q, "Link a TigerSystem account?", "Lier un compte TigerSystem ?", "TigerSystem-Konto verknüpfen?",
  "¿Vincular una cuenta TigerSystem?", "Collegare un account TigerSystem?", "绑定 TigerSystem 账户？",
  "Vincular uma conta TigerSystem?", "Ligar uma conta TigerSystem?", "Połączyć konto TigerSystem?",
  "Привязать аккаунт TigerSystem?", "Een TigerSystem-account koppelen?")
X(S_OB_ACCOUNT_SUB, "Your printers come from your account", "Vos imprimantes viennent de votre compte",
  "Ihre Drucker kommen aus Ihrem Konto", "Sus impresoras vienen de su cuenta",
  "Le stampanti arrivano dal tuo account", "打印机来自您的账户", "Suas impressoras vêm da sua conta",
  "As suas impressoras vêm da sua conta", "Drukarki pochodzą z Twojego konta",
  "Принтеры берутся из вашего аккаунта", "Je printers komen uit je account")
X(S_SETUP, "Set up", "Configurer", "Einrichten", "Configurar", "Configura", "设置", "Configurar", "Configurar",
  "Skonfiguruj", "Настроить", "Instellen")
X(S_LATER, "Later", "Plus tard", "Später", "Más tarde", "Più tardi", "稍后", "Mais tarde", "Mais tarde",
  "Później", "Позже", "Later")
X(S_LINK_NOW, "Link now", "Lier maintenant", "Jetzt verknüpfen", "Vincular ahora", "Collega ora", "立即绑定",
  "Vincular agora", "Ligar agora", "Połącz teraz", "Привязать сейчас", "Nu koppelen")
X(S_LINK_ACCOUNT, "Link an account", "Lier un compte", "Konto verbinden", "Vincular cuenta", "Collega account",
  "绑定账户", "Vincular conta", "Ligar conta", "Połącz konto", "Привязка аккаунта", "Account koppelen")
X(S_SIGN_IN, "Sign in", "Se connecter", "Anmelden", "Entrar", "Accedi", "登录", "Entrar", "Entrar", "Zaloguj",
  "Войти", "Inloggen")
X(S_CONTINUE_GOOGLE, "Continue with Google", "Continuer avec Google", "Weiter mit Google",
  "Continuar con Google", "Continua con Google", "使用 Google 继续", "Continuar com o Google",
  "Continuar com o Google", "Kontynuuj z Google", "Продолжить с Google", "Doorgaan met Google")
X(S_PAIR_SCAN, "Scan with your phone", "Scannez avec le téléphone", "Mit dem Handy scannen",
  "Escanee con el teléfono", "Scansiona col telefono", "用手机扫码", "Escaneie com o telefone",
  "Digitalize com o telemóvel", "Zeskanuj telefonem", "Сканируйте телефоном", "Scan met je telefoon")
X(S_PAIR_WAITING, "Waiting", "En attente", "Wartet", "Esperando", "In attesa", "等待中", "Aguardando",
  "A aguardar", "Oczekiwanie", "Ожидание", "Wachten")
X(S_PAIR_DONE, "Account linked", "Compte lié", "Konto verbunden", "Cuenta vinculada", "Account collegato",
  "账户已绑定", "Conta vinculada", "Conta ligada", "Konto połączone", "Аккаунт привязан", "Account gekoppeld")
X(S_PAIR_EXPIRED, "Code expired", "Code expiré", "Code abgelaufen", "Código expirado", "Codice scaduto",
  "二维码已过期", "Código expirado", "Código expirado", "Kod wygasł", "Код истёк", "Code verlopen")
X(S_PAIR_DENIED, "Refused", "Refusé", "Abgelehnt", "Rechazado", "Rifiutato", "已拒绝", "Recusado", "Recusado",
  "Odrzucono", "Отклонено", "Geweigerd")
X(S_PAIR_FAILED, "Failed", "Échec", "Fehlgeschlagen", "Fallo", "Fallito", "绑定失败", "Falhou", "Falhou",
  "Niepowodzenie", "Сбой", "Mislukt")
X(S_ACC_PROMPT_Q, "Connect your account", "Connectez votre compte", "Konto verbinden", "Conecte su cuenta",
  "Collega il tuo account", "连接您的账户", "Conecte sua conta", "Ligue a sua conta", "Połącz swoje konto",
  "Подключите аккаунт", "Verbind je account")

/* ---- Firmware update (net/ota.c), worded as on TigerScale V3 ---------- */
X(S_UPDATE, "Update", "Mise à jour", "Aktualisierung", "Actualización", "Aggiornamento", "更新", "Atualização",
  "Atualização", "Aktualizacja", "Обновление", "Update")
X(S_UPD_FOUND, "New firmware available. Go to the update page?", "Nouveau firmware disponible. Aller à la page de mise à jour ?", "Neue Firmware verfügbar. Zur Update-Seite gehen?", "Nuevo firmware disponible. ¿Ir a la página de actualización?", "Nuovo firmware disponibile. Vai alla pagina aggiornamento?", "发现新固件。前往更新页面？", "Novo firmware disponível. Ir para a página de atualização?", "Novo firmware disponível. Ir para a página de actualização?", "Nowy firmware dostępny. Przejść do strony aktualizacji?", "Доступна новая прошивка. Перейти к обновлению?", "Nieuwe firmware beschikbaar. Naar de updatepagina gaan?")
X(S_UPD_CURRENT, "Installed version", "Version installée", "Installierte Version", "Versión instalada",
  "Versione installata", "已安装版本", "Versão instalada", "Versão instalada", "Zainstalowana wersja",
  "Установленная версия", "Geïnstalleerde versie")
X(S_UPD_NEW, "New version", "Nouvelle version", "Neue Version", "Nueva versión", "Nuova versione", "新版本",
  "Nova versão", "Nova versão", "Nowa wersja", "Новая версия", "Nieuwe versie")
X(S_UPD_INSTALL, "Install", "Installer", "Installieren", "Instalar", "Installa", "安装", "Instalar", "Instalar",
  "Zainstaluj", "Установить", "Installeren")
X(S_UPD_KEEPS, "Wi-Fi, account and settings are kept", "Wi-Fi, compte et réglages sont conservés",
  "WLAN, Konto und Einstellungen bleiben erhalten", "Wi-Fi, cuenta y ajustes se conservan",
  "Wi-Fi, account e impostazioni sono conservati", "Wi-Fi、账户和设置将保留",
  "Wi-Fi, conta e configurações são mantidos", "Wi-Fi, conta e definições são mantidos",
  "Wi-Fi, konto i ustawienia zostaną zachowane", "Wi-Fi, аккаунт и настройки сохраняются",
  "Wifi, account en instellingen blijven behouden")
X(S_UPD_LATEST, "Your Tiger Touch is up to date", "Votre Tiger Touch est à jour", "Ihr Tiger Touch ist aktuell",
  "Su Tiger Touch está actualizado", "Il tuo Tiger Touch è aggiornato", "您的 Tiger Touch 已是最新",
  "Seu Tiger Touch está atualizado", "O seu Tiger Touch está atualizado", "Twój Tiger Touch jest aktualny",
  "Ваш Tiger Touch обновлён", "Je Tiger Touch is up-to-date")
X(S_UPD_CHECK_FAILED, "Could not check for updates", "Vérification impossible", "Prüfung nicht möglich",
  "No se pudo comprobar", "Verifica non riuscita", "无法检查更新", "Não foi possível verificar",
  "Não foi possível verificar", "Nie można sprawdzić", "Не удалось проверить обновления",
  "Kon niet op updates controleren")
X(S_UPD_NO_WIFI, "Wi-Fi required", "Wi-Fi requis", "WLAN erforderlich", "Wi-Fi requerido", "Wi-Fi richiesto",
  "需要 Wi-Fi", "Wi-Fi necessário", "Wi-Fi necessário", "Wymagane Wi-Fi", "Нужен Wi-Fi", "Wifi vereist")
X(S_UPD_CONFIRM, "Confirm update?", "Confirmer la mise à jour ?", "Update bestätigen?", "¿Confirmar actualización?",
  "Confermare aggiornamento?", "确认更新？", "Confirmar atualização?", "Confirmar atualização?",
  "Potwierdzić aktualizację?", "Подтвердить обновление?", "Update bevestigen?")
X(S_UPD_CONFIRM_WARN, "About 2 minutes. Do not power off the Tiger Touch.",
  "Environ 2 minutes. Ne pas éteindre le Tiger Touch.", "Etwa 2 Minuten. Tiger Touch nicht ausschalten.",
  "Unos 2 minutos. No apague el Tiger Touch.", "Circa 2 minuti. Non spegnere il Tiger Touch.",
  "约需 2 分钟，请勿断电。", "Cerca de 2 minutos. Não desligue o Tiger Touch.",
  "Cerca de 2 minutos. Não desligue o Tiger Touch.", "Około 2 minut. Nie wyłączaj Tiger Touch.",
  "Около 2 минут. Не выключайте Tiger Touch.", "Ongeveer 2 minuten. Zet de Tiger Touch niet uit.")
X(S_UPD_INSTALLING, "Installing", "Installation", "Installation", "Instalando", "Installazione", "安装中",
  "Instalando", "A instalar", "Instalowanie", "Установка", "Installeren")
X(S_UPD_DO_NOT_POWER_OFF, "Do not power off", "Ne pas éteindre", "Nicht ausschalten", "No apagar", "Non spegnere",
  "请勿断电", "Não desligar", "Não desligar", "Nie wyłączaj", "Не выключайте", "Niet uitschakelen")
X(S_UPD_REBOOTING, "Rebooting", "Redémarrage", "Neustart", "Reiniciando", "Riavvio", "重启中", "Reiniciando",
  "A reiniciar", "Ponowne uruchamianie", "Перезапуск", "Opnieuw opstarten")
X(S_UPD_FAILED, "Update failed", "Mise à jour échouée", "Update fehlgeschlagen", "Error en la actualización",
  "Aggiornamento fallito", "更新失败", "Falha na atualização", "Falha na atualização", "Aktualizacja nieudana",
  "Сбой обновления", "Update mislukt")
X(S_RETRY, "Try again", "Réessayer", "Erneut versuchen", "Intentar de nuevo", "Riprova", "重试",
  "Tentar novamente", "Tentar novamente", "Spróbuj ponownie", "Повторить", "Opnieuw proberen")
X(S_YES, "Yes", "Oui", "Ja", "Sí", "Sì", "是", "Sim", "Sim", "Tak", "Да", "Ja")
X(S_NO, "No", "Non", "Nein", "No", "No", "否", "Não", "Não", "Nie", "Нет", "Nee")

/* ---- Printers page ---------------------------------------------------- */
X(S_LOAD, "Load", "Charge", "Auslastung", "Carga", "Carico", "负载", "Carga", "Carga", "Obciążenie",
  "Нагрузка", "Belasting")
X(S_NOT_ENOUGH_ROOM, "Not enough room", "Pas assez de place", "Nicht genug Platz",
  "No hay espacio suficiente", "Spazio insufficiente", "空间不足", "Espaço insuficiente",
  "Espaço insuficiente", "Za mało miejsca", "Недостаточно места", "Niet genoeg ruimte")
X(S_LOAD_HINT, "Switched-on printers stay connected and show up instantly.",
  "Les imprimantes activées restent connectées : leurs données s'affichent aussitôt.",
  "Aktivierte Drucker bleiben verbunden und erscheinen sofort.",
  "Las impresoras activadas siguen conectadas y aparecen al instante.",
  "Le stampanti attivate restano connesse e compaiono subito.", "已开启的打印机保持连接，可即时显示。",
  "As impressoras ativadas continuam conectadas e aparecem na hora.",
  "As impressoras ativadas continuam ligadas e aparecem logo.",
  "Włączone drukarki pozostają połączone, więc pojawiają się od razu.",
  "Включённые принтеры остаются на связи и отображаются мгновенно.",
  "Ingeschakelde printers blijven verbonden en verschijnen meteen.")
X(S_SOON, "Soon", "Bientôt", "Bald", "Pronto", "Presto", "即将支持", "Em breve", "Em breve", "Wkrótce",
  "Скоро", "Binnenkort")
X(S_OFFLINE, "Offline", "Hors ligne", "Offline", "Desconectado", "Offline", "离线", "Offline", "Offline",
  "Offline", "Офлайн", "Offline")
X(S_LOCAL_NETWORK, "Local network", "Réseau local", "Lokales Netzwerk", "Red local", "Rete locale",
  "本地网络", "Rede local", "Rede local", "Sieć lokalna", "Локальная сеть", "Lokaal netwerk")
X(S_VIA_CLOUD, "via the cloud", "via le cloud", "über die Cloud", "a través de la nube", "tramite cloud",
  "通过云端", "pela nuvem", "através da cloud", "przez chmurę", "через облако", "via de cloud")
X(S_ADDRESS_UNKNOWN, "unknown address", "adresse inconnue", "Adresse unbekannt", "dirección desconocida",
  "indirizzo sconosciuto", "地址未知", "endereço desconhecido", "endereço desconhecido", "nieznany adres",
  "адрес неизвестен", "adres onbekend")
X(S_NO_PRINTER_ON, "No printer switched on", "Aucune imprimante activée", "Kein Drucker aktiviert",
  "Ninguna impresora activada", "Nessuna stampante attivata", "没有已开启的打印机",
  "Nenhuma impressora ativada", "Nenhuma impressora ativada", "Brak włączonych drukarek",
  "Нет включённых принтеров", "Geen printer ingeschakeld")
X(S_NO_PRINTER_ON_HINT, "Switch one on in Settings > Printers.", "Active-en une dans Paramètres > Imprimantes.",
  "Aktiviere einen unter Einstellungen > Drucker.", "Activa una en Ajustes > Impresoras.",
  "Attivane una in Impostazioni > Stampanti.", "请在 设置 > 打印机 中开启。",
  "Ative uma em Configurações > Impressoras.", "Ative uma em Definições > Impressoras.",
  "Włącz drukarkę w Ustawienia > Drukarki.", "Включите принтер в Настройки > Принтеры.",
  "Schakel er een in via Instellingen > Printers.")
X(S_PRINTERS_UNREAD_HINT, "Printers not read yet. Retrying.", "Imprimantes non lues. Nouvel essai.",
  "Drucker nicht gelesen. Neuer Versuch.", "Impresoras no leídas. Reintentando.",
  "Stampanti non lette. Nuovo tentativo.", "尚未读取打印机，正在重试。", "Impressoras não lidas. Tentando de novo.",
  "Impressoras não lidas. A tentar de novo.", "Drukarki nieodczytane. Ponawianie.",
  "Принтеры не прочитаны. Повтор.", "Printers niet gelezen. Opnieuw.")
X(S_ADD_PRINTER_HINT, "Add a printer in Tiger Studio.", "Ajoute une imprimante dans Tiger Studio.",
  "Füge in Tiger Studio einen Drucker hinzu.", "Añade una impresora en Tiger Studio.",
  "Aggiungi una stampante in Tiger Studio.", "请在 Tiger Studio 中添加打印机。",
  "Adicione uma impressora no Tiger Studio.", "Adicione uma impressora no Tiger Studio.",
  "Dodaj drukarkę w Tiger Studio.", "Добавьте принтер в Tiger Studio.",
  "Voeg een printer toe in Tiger Studio.")
X(S_NO_PRINTER, "No printer", "Aucune imprimante", "Kein Drucker", "Ninguna impresora",
  "Nessuna stampante", "无打印机", "Nenhuma impressora", "Nenhuma impressora", "Brak drukarki",
  "Нет принтера", "Geen printer")
X(S_BRAND_SOON, "This brand isn't supported yet.", "Cette marque n'est pas encore prise en charge.",
  "Diese Marke wird noch nicht unterstützt.", "Esta marca aún no es compatible.",
  "Questo marchio non è ancora supportato.", "暂不支持该品牌。", "Esta marca ainda não é compatível.",
  "Esta marca ainda não é suportada.", "Ta marka nie jest jeszcze obsługiwana.",
  "Этот бренд пока не поддерживается.", "Dit merk wordt nog niet ondersteund.")
X(S_CONNECTING, "Connecting…", "Connexion…", "Verbinden…", "Conectando…",
  "Connessione…", "正在连接…", "Conectando…", "A ligar…", "Łączenie…", "Подключение…",
  "Verbinden…")
X(S_EXT, "Ext.", "Ext.", "Ext.", "Ext.", "Est.", "外置", "Ext.", "Ext.", "Zewn.", "Внеш.", "Ext.")
X(S_BAMBU_REFUSED, "Bambu session refused: renew it in Tiger Studio.",
  "Session Bambu refusée : renouvelle-la dans Tiger Studio.",
  "Bambu-Sitzung abgelehnt: Erneuere sie in Tiger Studio.",
  "Sesión de Bambu rechazada: renuévala en Tiger Studio.",
  "Sessione Bambu rifiutata: rinnovala in Tiger Studio.", "Bambu 会话被拒绝：请在 Tiger Studio 中重新登录。",
  "Sessão da Bambu recusada: renove-a no Tiger Studio.", "Sessão da Bambu recusada: renove-a no Tiger Studio.",
  "Sesja Bambu odrzucona: odnów ją w Tiger Studio.", "Сессия Bambu отклонена: обновите её в Tiger Studio.",
  "Bambu-sessie geweigerd: vernieuw die in Tiger Studio.")
X(S_BAMBU_MISSING, "No Bambu session: sign in to Bambu in Tiger Studio.",
  "Session Bambu absente : connecte-toi à Bambu dans Tiger Studio.",
  "Keine Bambu-Sitzung: Melde dich in Tiger Studio bei Bambu an.",
  "Sin sesión de Bambu: inicia sesión en Bambu desde Tiger Studio.",
  "Nessuna sessione Bambu: accedi a Bambu da Tiger Studio.", "没有 Bambu 会话：请在 Tiger Studio 中登录 Bambu。",
  "Sem sessão da Bambu: entre na Bambu pelo Tiger Studio.",
  "Sem sessão da Bambu: inicie sessão na Bambu no Tiger Studio.",
  "Brak sesji Bambu: zaloguj się do Bambu w Tiger Studio.",
  "Нет сессии Bambu: войдите в Bambu через Tiger Studio.",
  "Geen Bambu-sessie: log in bij Bambu via Tiger Studio.")
X(S_CLOUD_FAIL, "Can't reach the cloud.", "Connexion cloud impossible.", "Cloud nicht erreichbar.",
  "No se puede conectar a la nube.", "Impossibile raggiungere il cloud.", "无法连接云端。",
  "Não foi possível acessar a nuvem.", "Não foi possível aceder à cloud.", "Brak połączenia z chmurą.",
  "Нет связи с облаком.", "Cloud niet bereikbaar.")

X(S_ACU_NOT_PAIRED, "Pair it once in AnycubicSlicerNext, then sync in Tiger Studio.",
  "Appaire-la une fois dans AnycubicSlicerNext, puis synchronise dans Tiger Studio.",
  "Einmal in AnycubicSlicerNext koppeln, dann in Tiger Studio synchronisieren.",
  "Víncula la una vez en AnycubicSlicerNext y sincroniza en Tiger Studio.",
  "Associala una volta in AnycubicSlicerNext, poi sincronizza in Tiger Studio.",
  "请先在 AnycubicSlicerNext 中配对，再在 Tiger Studio 中同步。",
  "Pareie uma vez no AnycubicSlicerNext e sincronize no Tiger Studio.",
  "Emparelhe uma vez no AnycubicSlicerNext e sincronize no Tiger Studio.",
  "Sparuj ją raz w AnycubicSlicerNext i zsynchronizuj w Tiger Studio.",
  "Подключите её в AnycubicSlicerNext и синхронизируйте в Tiger Studio.",
  "Koppel hem één keer in AnycubicSlicerNext en synchroniseer in Tiger Studio.")
X(S_ERR_API_KEY, "The printer refused its API key: check it in Tiger Studio.",
  "L'imprimante refuse sa clé API : vérifie-la dans Tiger Studio.",
  "Der Drucker lehnt seinen API-Schlüssel ab: in Tiger Studio prüfen.",
  "La impresora rechaza su clave API: revísala en Tiger Studio.",
  "La stampante rifiuta la sua chiave API: controllala in Tiger Studio.",
  "打印机拒绝了其 API 密钥：请在 Tiger Studio 中检查。",
  "A impressora recusou a chave de API: verifique-a no Tiger Studio.",
  "A impressora recusou a chave de API: verifique-a no Tiger Studio.",
  "Drukarka odrzuca swój klucz API: sprawdź go w Tiger Studio.",
  "Принтер отклонил ключ API: проверьте его в Tiger Studio.",
  "De printer weigert zijn API-sleutel: controleer hem in Tiger Studio.")
X(S_KLIPPY_NOT_READY, "Klipper is not ready:", "Klipper n'est pas prêt :", "Klipper ist nicht bereit:",
  "Klipper no está listo:", "Klipper non è pronto:", "Klipper 未就绪：", "O Klipper não está pronto:",
  "O Klipper não está pronto:", "Klipper nie jest gotowy:", "Klipper не готов:", "Klipper is niet klaar:")
X(S_OCTO_NO_PRINTER, "OctoPrint answers, but no printer is connected to it.",
  "OctoPrint répond, mais aucune imprimante n'y est connectée.",
  "OctoPrint antwortet, aber kein Drucker ist verbunden.",
  "OctoPrint responde, pero no hay ninguna impresora conectada.",
  "OctoPrint risponde, ma nessuna stampante è collegata.",
  "OctoPrint 有响应，但没有连接打印机。",
  "O OctoPrint responde, mas nenhuma impressora está conectada.",
  "O OctoPrint responde, mas nenhuma impressora está ligada.",
  "OctoPrint odpowiada, ale nie ma podłączonej drukarki.",
  "OctoPrint отвечает, но принтер к нему не подключён.",
  "OctoPrint antwoordt, maar er is geen printer verbonden.")
X(S_ACU_CLOUD_NO_ID, "Its cloud sign-in is missing: refresh this printer in Tiger Studio.",
  "Ses identifiants cloud manquent : actualise cette imprimante dans Tiger Studio.",
  "Die Cloud-Anmeldung fehlt: Drucker in Tiger Studio aktualisieren.",
  "Falta su inicio de sesión en la nube: actualiza esta impresora en Tiger Studio.",
  "Manca l'accesso cloud: aggiorna questa stampante in Tiger Studio.",
  "缺少云端登录信息：请在 Tiger Studio 中刷新此打印机。",
  "Falta o login na nuvem: atualize esta impressora no Tiger Studio.",
  "Falta o início de sessão na cloud: atualize esta impressora no Tiger Studio.",
  "Brak logowania do chmury: odśwież tę drukarkę w Tiger Studio.",
  "Нет данных входа в облако: обновите этот принтер в Tiger Studio.",
  "Cloudaanmelding ontbreekt: vernieuw deze printer in Tiger Studio.")
X(S_ACU_CLOUD_REFUSED, "Anycubic refused the sign-in: refresh this printer in Tiger Studio.",
  "Anycubic a refusé la connexion : actualise cette imprimante dans Tiger Studio.",
  "Anycubic hat die Anmeldung abgelehnt: Drucker in Tiger Studio aktualisieren.",
  "Anycubic rechazó el inicio de sesión: actualiza esta impresora en Tiger Studio.",
  "Anycubic ha rifiutato l'accesso: aggiorna questa stampante in Tiger Studio.",
  "Anycubic 拒绝了登录：请在 Tiger Studio 中刷新此打印机。",
  "A Anycubic recusou o login: atualize esta impressora no Tiger Studio.",
  "A Anycubic recusou o início de sessão: atualize esta impressora no Tiger Studio.",
  "Anycubic odrzucił logowanie: odśwież tę drukarkę w Tiger Studio.",
  "Anycubic отклонил вход: обновите этот принтер в Tiger Studio.",
  "Anycubic weigerde de aanmelding: vernieuw deze printer in Tiger Studio.")

/* ---- Wi-Fi page ------------------------------------------------------- */
X(S_IP, "IP", "IP", "IP", "IP", "IP", "IP", "IP", "IP", "IP", "IP", "IP")
X(S_MAC, "MAC", "MAC", "MAC", "MAC", "MAC", "MAC", "MAC", "MAC", "MAC", "MAC", "MAC")
X(S_CHANNEL, "Channel", "Canal", "Kanal", "Canal", "Canale", "信道", "Canal", "Canal", "Kanał", "Канал",
  "Kanaal")
X(S_CHANGE_NETWORK, "Change network", "Changer de réseau", "Netzwerk wechseln", "Cambiar de red",
  "Cambia rete", "切换网络", "Trocar de rede", "Mudar de rede", "Zmień sieć", "Сменить сеть",
  "Ander netwerk kiezen")
X(S_Q_EXCELLENT, "Excellent", "Excellent", "Ausgezeichnet", "Excelente", "Eccellente", "极佳", "Excelente",
  "Excelente", "Doskonały", "Отличный", "Uitstekend")
X(S_Q_GOOD, "Good", "Bon", "Gut", "Bueno", "Buono", "良好", "Bom", "Bom", "Dobry", "Хороший", "Goed")
X(S_Q_FAIR, "Fair", "Correct", "Mittel", "Aceptable", "Discreto", "一般", "Razoável", "Razoável", "Średni",
  "Средний", "Redelijk")
X(S_Q_WEAK, "Weak", "Faible", "Schwach", "Débil", "Debole", "弱", "Fraco", "Fraco", "Słaby", "Слабый",
  "Zwak")
X(S_NO_ACCOUNT, "No account", "Aucun compte", "Kein Konto", "Sin cuenta", "Nessun account", "无账户", "Sem conta",
  "Sem conta", "Brak konta", "Нет аккаунта", "Geen account")
X(S_NOT_CONNECTED, "Not connected", "Non connecté", "Nicht verbunden", "Sin conexión", "Non connesso",
  "未连接", "Sem conexão", "Sem ligação", "Brak połączenia", "Нет подключения", "Niet verbonden")

/* ---- Screen page ------------------------------------------------------ */
X(S_BRIGHTNESS, "Brightness", "Luminosité", "Helligkeit", "Brillo", "Luminosità", "亮度", "Brilho",
  "Brilho", "Jasność", "Яркость", "Helderheid")
X(S_SLEEP_AFTER, "Sleep after", "Veille après", "Ruhezustand nach", "Suspender tras", "Standby dopo",
  "休眠时间", "Repouso após", "Suspensão após", "Uśpienie po", "Спящий режим через", "Slaapstand na")
X(S_NEVER, "Never", "Jamais", "Nie", "Nunca", "Mai", "从不", "Nunca", "Nunca", "Nigdy", "Никогда", "Nooit")
X(S_ORIENTATION, "Orientation", "Orientation", "Ausrichtung", "Orientación", "Orientamento", "方向",
  "Orientação", "Orientação", "Orientacja", "Ориентация", "Oriëntatie")
X(S_BATTERY, "Built-in battery", "Batterie intégrée", "Eingebauter Akku", "Batería integrada",
  "Batteria integrata", "内置电池", "Bateria interna", "Bateria interna", "Wbudowany akumulator",
  "Встроенный аккумулятор", "Ingebouwde accu")
X(S_BATT_TITLE, "Battery", "Batterie", "Akku", "Batería", "Batteria", "电池", "Bateria", "Bateria", "Akumulator", "Аккумулятор", "Accu")
X(S_BATT_INFO1, "The Panda Touch and the K-Touch have a battery inside: about 30 minutes away from the dock. The switch at the back chooses Off, Battery or DC 5V.", "Le Panda Touch et le K-Touch ont une batterie intégrée : environ 30 minutes hors du socle. Le sélecteur au dos choisit Off, Batterie ou DC 5V.", "Panda Touch und K-Touch haben einen eingebauten Akku: etwa 30 Minuten ohne Dock. Der Schalter auf der Rückseite wählt Off, Akku oder DC 5V.", "El Panda Touch y el K-Touch llevan una batería interna: unos 30 minutos fuera de la base. El interruptor trasero elige Off, Batería o DC 5V.", "Panda Touch e K-Touch hanno una batteria interna: circa 30 minuti fuori dalla base. L'interruttore sul retro sceglie Off, Batteria o DC 5V.", "Panda Touch 和 K-Touch 内置电池：离开底座约可使用 30 分钟。背面的开关可选择 Off、电池或 DC 5V。", "O Panda Touch e o K-Touch têm uma bateria interna: cerca de 30 minutos fora da base. A chave atrás escolhe Off, Bateria ou DC 5V.", "O Panda Touch e o K-Touch têm uma bateria interna: cerca de 30 minutos fora da base. O interruptor atrás escolhe Off, Bateria ou DC 5V.", "Panda Touch i K-Touch mają wbudowany akumulator: około 30 minut poza stacją. Przełącznik z tyłu wybiera Off, Akumulator lub DC 5V.", "У Panda Touch и K-Touch есть встроенный аккумулятор: около 30 минут без док-станции. Переключатель сзади выбирает Off, Аккумулятор или DC 5V.", "De Panda Touch en de K-Touch hebben een ingebouwde accu: ongeveer 30 minuten buiten het dock. De schakelaar achterop kiest Off, Accu of DC 5V.")
X(S_BATT_INFO2, "This screen cannot measure its charge: no wire from the battery reaches its processor. What you declare here is sent to your TigerSystem account.", "Cet écran ne peut pas mesurer sa charge : aucun fil de la batterie n'arrive à son processeur. Ce que vous déclarez ici est envoyé à votre compte TigerSystem.", "Dieser Bildschirm kann seinen Ladestand nicht messen: Keine Leitung vom Akku erreicht den Prozessor. Was Sie hier angeben, wird an Ihr TigerSystem-Konto gesendet.", "Esta pantalla no puede medir su carga: ningún cable de la batería llega a su procesador. Lo que declares aquí se envía a tu cuenta TigerSystem.", "Questo schermo non può misurare la carica: nessun filo della batteria arriva al processore. Ciò che dichiari qui viene inviato al tuo account TigerSystem.", "此屏幕无法测量电量：电池没有任何线路连接到处理器。您在此处的设置会发送到您的 TigerSystem 账户。", "Esta tela não consegue medir a carga: nenhum fio da bateria chega ao processador. O que você declarar aqui é enviado para sua conta TigerSystem.", "Este ecrã não consegue medir a carga: nenhum fio da bateria chega ao processador. O que declarar aqui é enviado para a sua conta TigerSystem.", "Ten ekran nie może zmierzyć poziomu naładowania: żaden przewód z akumulatora nie dochodzi do procesora. To, co tu zadeklarujesz, trafia do twojego konta TigerSystem.", "Этот экран не может измерить заряд: от аккумулятора к процессору не идёт ни одного провода. Указанное здесь отправляется в ваш аккаунт TigerSystem.", "Dit scherm kan zijn lading niet meten: geen draad van de accu bereikt de processor. Wat u hier aangeeft, wordt naar uw TigerSystem-account gestuurd.")
X(S_DEMO, "Demo mode", "Mode démo", "Demo-Modus", "Modo demo", "Modalità demo", "演示模式", "Modo demo", "Modo demo", "Tryb demo", "Демо-режим", "Demomodus")
X(S_ON, "On", "Activé", "Ein", "Activado", "Attivo", "开启", "Ativado", "Ativado", "Włączony", "Вкл.", "Aan")
X(S_OFF, "Off", "Désactivé", "Aus", "Desactivado", "Disattivo", "关闭", "Desativado", "Desativado", "Wyłączony", "Выкл.", "Uit")
X(S_DEMO_INFO1, "For a showroom, a fair or screenshots: the screen shows ten made-up printers of every brand, printing, exactly as it shows real ones.", "Pour un showroom, un salon ou des captures d'écran : l'écran montre dix imprimantes inventées de toutes les marques, en impression, exactement comme de vraies.", "Für einen Showroom, eine Messe oder Screenshots: Der Bildschirm zeigt zehn erfundene Drucker aller Marken beim Drucken, genau wie echte.", "Para una tienda, una feria o capturas de pantalla: la pantalla muestra diez impresoras inventadas de todas las marcas, imprimiendo, igual que las reales.", "Per uno showroom, una fiera o degli screenshot: lo schermo mostra dieci stampanti inventate di ogni marca, in stampa, esattamente come quelle vere.", "用于展厅、展会或截图：屏幕会显示十台各品牌的虚拟打印机正在打印，与真实打印机的显示完全一样。", "Para um showroom, uma feira ou capturas de tela: a tela mostra dez impressoras inventadas de todas as marcas, imprimindo, exatamente como as reais.", "Para um showroom, uma feira ou capturas de ecrã: o ecrã mostra dez impressoras inventadas de todas as marcas, a imprimir, exatamente como as reais.", "Do salonu, na targi lub do zrzutów ekranu: ekran pokazuje dziesięć wymyślonych drukarek wszystkich marek w trakcie druku, dokładnie jak prawdziwe.", "Для шоурума, выставки или скриншотов: экран показывает десять вымышленных принтеров всех марок за печатью, в точности как настоящие.", "Voor een showroom, een beurs of screenshots: het scherm toont tien verzonnen printers van elk merk, aan het printen, precies zoals echte.")
X(S_DEMO_INFO2, "While it is on, none of your printers is reached and nothing is sent to any machine. The screen restarts when you switch it.", "Tant qu'il est activé, aucune de vos imprimantes n'est contactée et rien n'est envoyé à aucune machine. L'écran redémarre quand vous le changez.", "Solange er aktiv ist, wird keiner Ihrer Drucker angesprochen und nichts an eine Maschine gesendet. Beim Umschalten startet der Bildschirm neu.", "Mientras está activado, no se contacta ninguna de tus impresoras y no se envía nada a ninguna máquina. La pantalla se reinicia al cambiarlo.", "Finché è attiva, nessuna delle tue stampanti viene contattata e nulla viene inviato ad alcuna macchina. Lo schermo si riavvia quando la cambi.", "开启期间，不会连接您的任何打印机，也不会向任何机器发送内容。切换时屏幕会重新启动。", "Enquanto estiver ativado, nenhuma das suas impressoras é contatada e nada é enviado a nenhuma máquina. A tela reinicia quando você o muda.", "Enquanto estiver ativado, nenhuma das suas impressoras é contactada e nada é enviado a nenhuma máquina. O ecrã reinicia quando o muda.", "Gdy jest włączony, żadna z twoich drukarek nie jest łączona i nic nie jest wysyłane do żadnej maszyny. Ekran uruchamia się ponownie po zmianie.", "Пока он включён, ни один из ваших принтеров не опрашивается и ничего не отправляется ни на одну машину. При переключении экран перезапускается.", "Zolang hij aan staat, wordt geen van uw printers benaderd en wordt niets naar een machine gestuurd. Het scherm herstart wanneer u hem omzet.")
X(S_LAN_LIVE, "Live view", "Vue live", "Live-Ansicht", "Vista en directo", "Vista live", "实时画面", "Visualização ao vivo", "Visualização em direto", "Podgląd na żywo", "Просмотр вживую", "Liveweergave")
X(S_LAN_ADDRESS, "Address", "Adresse", "Adresse", "Dirección", "Indirizzo", "地址", "Endereço", "Endereço", "Adres", "Адрес", "Adres")
X(S_LAN_CODE, "Access code", "Code d'accès", "Zugangscode", "Código de acceso", "Codice di accesso", "访问码", "Código de acesso", "Código de acesso", "Kod dostępu", "Код доступа", "Toegangscode")
X(S_LAN_INFO, "Tiger Studio Manager, on the same Wi-Fi, uses this code to show this screen live, touch it and take screenshots of it. The code is also in your TigerSystem account.", "Tiger Studio Manager, sur le même Wi-Fi, utilise ce code pour afficher cet écran en direct, le toucher et en faire des captures. Le code est aussi dans votre compte TigerSystem.", "Tiger Studio Manager im selben WLAN nutzt diesen Code, um diesen Bildschirm live anzuzeigen, ihn zu bedienen und Screenshots davon zu machen. Der Code steht auch in Ihrem TigerSystem-Konto.", "Tiger Studio Manager, en la misma red Wi-Fi, usa este código para mostrar esta pantalla en directo, tocarla y hacer capturas. El código también está en tu cuenta TigerSystem.", "Tiger Studio Manager, sulla stessa rete Wi-Fi, usa questo codice per mostrare questo schermo dal vivo, toccarlo e farne degli screenshot. Il codice è anche nel tuo account TigerSystem.", "同一 Wi-Fi 下的 Tiger Studio Manager 使用此访问码实时显示此屏幕、进行触控并截图。访问码也保存在您的 TigerSystem 账户中。", "O Tiger Studio Manager, na mesma rede Wi-Fi, usa este código para mostrar esta tela ao vivo, tocá-la e fazer capturas dela. O código também está na sua conta TigerSystem.", "O Tiger Studio Manager, na mesma rede Wi-Fi, usa este código para mostrar este ecrã em direto, tocar-lhe e fazer capturas dele. O código também está na sua conta TigerSystem.", "Tiger Studio Manager w tej samej sieci Wi-Fi używa tego kodu, aby pokazywać ten ekran na żywo, dotykać go i robić jego zrzuty. Kod jest też na Twoim koncie TigerSystem.", "Tiger Studio Manager в той же сети Wi-Fi использует этот код, чтобы показывать этот экран вживую, управлять им и делать его снимки. Код также есть в вашей учётной записи TigerSystem.", "Tiger Studio Manager op hetzelfde wifi-netwerk gebruikt deze code om dit scherm live te tonen, het te bedienen en er screenshots van te maken. De code staat ook in uw TigerSystem-account.")
X(S_LAN_OFF_INFO, "Live view is off: nothing is served on the network.", "La vue live est désactivée : rien n'est servi sur le réseau.", "Die Live-Ansicht ist aus: Im Netzwerk wird nichts bereitgestellt.", "La vista en directo está desactivada: no se sirve nada en la red.", "La vista live è disattivata: nulla viene servito sulla rete.", "实时画面已关闭：网络上不提供任何内容。", "A visualização ao vivo está desativada: nada é servido na rede.", "A visualização em direto está desativada: nada é servido na rede.", "Podgląd na żywo jest wyłączony: nic nie jest udostępniane w sieci.", "Просмотр вживую выключен: в сети ничего не доступно.", "Liveweergave staat uit: er wordt niets op het netwerk aangeboden.")
X(S_STOP_Q, "Stop this print?", "Arrêter cette impression ?", "Diesen Druck stoppen?", "¿Detener esta impresión?", "Fermare questa stampa?", "停止此次打印？", "Parar esta impressão?", "Parar esta impressão?", "Zatrzymać ten wydruk?", "Остановить эту печать?", "Deze print stoppen?")
X(S_STOP_WARN, "The print ends here and cannot be resumed.", "L'impression s'arrête ici et ne pourra pas être reprise.", "Der Druck endet hier und kann nicht fortgesetzt werden.", "La impresión termina aquí y no se podrá reanudar.", "La stampa termina qui e non potrà essere ripresa.", "打印将在此结束，且无法继续。", "A impressão termina aqui e não poderá ser retomada.", "A impressão termina aqui e não poderá ser retomada.", "Wydruk zakończy się tutaj i nie będzie można go wznowić.", "Печать завершится, продолжить её будет нельзя.", "De print stopt hier en kan niet worden hervat.")
X(S_STOP, "Stop", "Arrêter", "Stoppen", "Detener", "Ferma", "停止", "Parar", "Parar", "Zatrzymaj", "Остановить", "Stoppen")
X(S_SLEEP_1M, "1 min", "1 min", "1 Min.", "1 min", "1 min", "1 分钟", "1 min", "1 min", "1 min", "1 мин",
  "1 min")
X(S_SLEEP_10M, "10 min", "10 min", "10 Min.", "10 min", "10 min", "10 分钟", "10 min", "10 min", "10 min",
  "10 мин", "10 min")
X(S_SLEEP_30M, "30 min", "30 min", "30 Min.", "30 min", "30 min", "30 分钟", "30 min", "30 min", "30 min",
  "30 мин", "30 min")
X(S_SLEEP_1H, "1 h", "1 h", "1 Std.", "1 h", "1 h", "60 分钟", "1 h", "1 h", "1 godz.", "1 ч", "1 uur")

/* ---- Dashboard -------------------------------------------------------- */
X(S_LAYER, "Layer %d / %d", "Couche %d / %d", "Schicht %d / %d", "Capa %d / %d", "Strato %d / %d",
  "层 %d / %d", "Camada %d / %d", "Camada %d / %d", "Warstwa %d / %d", "Слой %d / %d", "Laag %d / %d")
X(S_FILE, "File", "Fichier", "Datei", "Archivo", "File", "文件", "Arquivo", "Ficheiro", "Plik", "Файл",
  "Bestand")
X(S_TIME_LEFT, "Time left", "Temps restant", "Restzeit", "Tiempo restante", "Tempo rimanente", "剩余时间",
  "Tempo restante", "Tempo restante", "Pozostały czas", "Осталось", "Resterende tijd")
X(S_END_ESTIMATED, "Estimated end", "Fin estimée", "Geschätztes Ende", "Fin estimado", "Fine stimata",
  "预计完成", "Término estimado", "Fim estimado", "Szacowany koniec", "Окончание", "Verwacht einde")
/* Agrees with the word for "nozzle" of each column: buse (f), ugello (m), bico (m), bocal (m), dysza (f), сопло (n). */
X(S_NOZZLE_LEFT, "Left", "Gauche", "Links", "Izquierda", "Sinistro", "左", "Esquerdo", "Esquerdo", "Lewa", "Левое", "Links")
X(S_NOZZLE_RIGHT, "Right", "Droite", "Rechts", "Derecha", "Destro", "右", "Direito", "Direito", "Prawa", "Правое", "Rechts")
/* One format, %d: the nozzle's number, for a printer with several. */
X(S_NOZZLE_N, "Nozzle %d", "Buse %d", "Düse %d", "Boquilla %d", "Ugello %d", "喷嘴 %d", "Bico %d", "Bocal %d", "Dysza %d", "Сопло %d", "Nozzle %d")
X(S_ST_PRINTING, "Printing", "Impression", "Druckt", "Imprimiendo", "In stampa", "打印中", "Imprimindo",
  "A imprimir", "Drukuje", "Печать", "Printen")
X(S_ST_PAUSED, "Paused", "Pause", "Pausiert", "En pausa", "In pausa", "已暂停", "Pausado", "Em pausa",
  "Wstrzymano", "На паузе", "Gepauzeerd")
X(S_ST_DONE, "Done", "Terminé", "Fertig", "Hecho", "Fatto", "完成", "Concluído", "Concluído", "Gotowe",
  "Готово", "Klaar")
X(S_ST_ERROR, "Error", "Erreur", "Fehler", "Error", "Errore", "错误", "Erro", "Erro", "Błąd", "Ошибка",
  "Fout")
X(S_ST_IDLE, "Idle", "Repos", "Bereit", "En reposo", "A riposo", "空闲", "Em repouso", "Em repouso",
  "Bezczynna", "Простой", "Inactief")
X(S_TIMEZONE, "Time zone", "Fuseau horaire", "Zeitzone", "Zona horaria", "Fuso orario",
  "时区", "Fuso horário", "Fuso horário", "Strefa czasowa", "Часовой пояс", "Tijdzone")
X(S_TZ_AUTO, "Automatic", "Automatique", "Automatisch", "Automático", "Automatico",
  "自动", "Automático", "Automático", "Automatycznie", "Автоматически", "Automatisch")
X(S_TZ_SUMMER, "summer time", "heure d'été", "Sommerzeit", "horario de verano", "ora legale",
  "夏令时", "horário de verão", "hora de verão", "czas letni", "летнее время", "zomertijd")
X(S_TZ_WINTER, "winter time", "heure d'hiver", "Winterzeit", "horario de invierno", "ora solare",
  "冬令时", "horário de inverno", "hora de inverno", "czas zimowy", "зимнее время", "wintertijd")
