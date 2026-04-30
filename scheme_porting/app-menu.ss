(begin
  (define winscheme-app-menu-raw-scheme-dir
    (foreign-procedure "winscheme_scheme_dir_utf8" () string))

  (define (winscheme-app-menu-scheme-dir)
    (winscheme-app-menu-raw-scheme-dir))

  (load (string-append (winscheme-app-menu-scheme-dir) "\\json.ss"))

  (define winscheme-app-menu-raw-replace-json
    (foreign-procedure "winscheme_app_menu_replace_json" (string) integer-64))
  (define winscheme-app-menu-raw-clear
    (foreign-procedure "winscheme_app_menu_clear" () integer-64))
  (define winscheme-app-menu-raw-last-error
    (foreign-procedure "winscheme_app_menu_last_error" () string))

  (define winscheme-app-menu-handlers '())

  (define (winscheme-app-menu-last-error)
    (let ((message (winscheme-app-menu-raw-last-error)))
      (if (string? message) message "")))

  (define (winscheme-app-menu-fail who . irritants)
    (apply assertion-violation
           who
           (let ((message (winscheme-app-menu-last-error)))
             (if (> (string-length message) 0)
                 message
                 "Editor app menu operation failed"))
           irritants))

  (define (app-menu-key value)
    (cond
      ((symbol? value) (symbol->string value))
      ((string? value) value)
      (else
       (assertion-violation 'app-menu-key "menu command ids must be symbols or strings" value))))

  (define (app-menu-entry-key? value)
    (or (symbol? value) (string? value)))

  (define (app-menu-props-alist? value)
    (and (list? value)
         (let loop ((rest value))
           (or (null? rest)
               (and (pair? (car rest))
                    (app-menu-entry-key? (caar rest))
                    (loop (cdr rest)))))))

  (define (app-menu-normalize value)
    (cond
      ((pair? value)
       (if (and (pair? (car value))
                (or (symbol? (caar value)) (string? (caar value))))
           (map (lambda (entry)
                  (cons (app-menu-key (car entry))
                        (app-menu-normalize (cdr entry))))
                value)
           (map app-menu-normalize value)))
      ((vector? value)
       (list->vector (map app-menu-normalize (vector->list value))))
      ((symbol? value)
       (symbol->string value))
      (else value)))

  (define (app-menu title items)
    `((title . ,title)
      (items . ,items)))

  (define (app-menu-separator)
    '((separator . #t)))

  (define (app-menu-submenu text items)
    `((text . ,text)
      (items . ,items)))

  (define (app-menu-item* id text props . maybe-handler)
    (when (and (pair? maybe-handler) (procedure? (car maybe-handler)))
      (winscheme-app-menu-register-handler! id (car maybe-handler)))
    (append
      `((id . ,(app-menu-key id))
        (text . ,text))
      props))

  (define (app-menu-item id text . rest)
    (let ((props '())
          (handler #f))
      (cond
        ((null? rest) (void))
        ((procedure? (car rest))
         (set! handler (car rest)))
        ((app-menu-props-alist? (car rest))
         (set! props (car rest))
         (when (and (pair? (cdr rest)) (procedure? (cadr rest)))
           (set! handler (cadr rest))))
        (else
         (assertion-violation 'app-menu-item "expected handler procedure or property alist" rest)))
      (if handler
          (app-menu-item* id text props handler)
          (app-menu-item* id text props))))

  (define (app-menu-check-item id text checked? . maybe-handler)
    (if (and (pair? maybe-handler) (procedure? (car maybe-handler)))
        (app-menu-item* id text `((checked . ,checked?)) (car maybe-handler))
        (app-menu-item* id text `((checked . ,checked?)))))

  (define (app-menu-radio-item id text group checked? . maybe-handler)
    (let ((props `((radio-group . ,(app-menu-key group))
                   (checked . ,checked?))))
      (if (and (pair? maybe-handler) (procedure? (car maybe-handler)))
          (app-menu-item* id text props (car maybe-handler))
          (app-menu-item* id text props))))

  (define (app-menu-disabled-item id text . maybe-handler)
    (if (and (pair? maybe-handler) (procedure? (car maybe-handler)))
        (app-menu-item* id text '((enabled . #f)) (car maybe-handler))
        (app-menu-item* id text '((enabled . #f)))))

  (define (workspace-menu items)
    (app-menu "Workspace" items))

  (define (app-menu-bar menus)
    `((menus . ,menus)))

  (define (winscheme-app-menu-register-handler! id proc)
    (unless (procedure? proc)
      (assertion-violation 'winscheme-app-menu-register-handler! "expected procedure" proc))
    (let ((key (app-menu-key id)))
      (set! winscheme-app-menu-handlers
            (cons (cons key proc)
                  (let loop ((rest winscheme-app-menu-handlers))
                    (cond
                      ((null? rest) '())
                      ((string=? (caar rest) key) (loop (cdr rest)))
                      (else (cons (car rest) (loop (cdr rest)))))))))
    #t)

  (define (winscheme-app-menu-clear-handlers!)
    (set! winscheme-app-menu-handlers '())
    #t)

  (define (winscheme-app-menu-dispatch! id)
    (let ((key (app-menu-key id)))
      (let ((entry (assoc key winscheme-app-menu-handlers)))
        (if entry
            ((cdr entry))
            (assertion-violation 'winscheme-app-menu-dispatch! "unknown app menu command" key)))))

  (define (app-menu-set! spec)
    (let ((payload (json->string (app-menu-normalize spec))))
      (if (not (zero? (winscheme-app-menu-raw-replace-json payload)))
          #t
          (winscheme-app-menu-fail 'app-menu-set! spec))))

  (define (workspace-menu-set! items)
    (app-menu-set! (workspace-menu items)))

  (define (app-menu-bar-set! menus)
    (app-menu-set! (app-menu-bar menus)))

  (define (app-menu-clear!)
    (winscheme-app-menu-clear-handlers!)
    (if (not (zero? (winscheme-app-menu-raw-clear)))
        #t
        (winscheme-app-menu-fail 'app-menu-clear!)))
)
