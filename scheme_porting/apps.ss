(begin
  (define winscheme-raw-user-apps-dir
    (foreign-procedure "winscheme_user_apps_dir_utf8" () string))

  (define winscheme-raw-examples-dir
    (foreign-procedure "winscheme_examples_dir_utf8" () string))

  (define winscheme-raw-refresh-apps
    (foreign-procedure "winscheme_refresh_apps" () integer-64))

  (define winscheme-app-registry
    (list
      (list
        (cons 'id 'user-app-demo)
        (cons 'title "Customer Workspace Demo")
        (cons 'summary "A small declarative customer workspace with forms, notes, and native service buttons.")
        (cons 'file "user-app-demo.ss"))
      (list
        (cons 'id 'user-app-state-demo)
        (cons 'title "User App State Store Demo")
        (cons 'summary "A focused example showing the lightweight state store and patch-based UI updates.")
        (cons 'file "user-app-state-demo.ss"))
      (list
        (cons 'id 'user-app-widget-demo)
        (cons 'title "Widget Gallery")
        (cons 'summary "A broad interactive gallery of declarative controls, menus, shortcuts, and services.")
        (cons 'file "user-app-widget-demo.ss"))
      (list
        (cons 'id 'user-app-weather-demo)
        (cons 'title "Weather Desk")
        (cons 'summary "A networked app that fetches weather data and presents a clickable forecast table.")
        (cons 'file "user-app-weather-demo.ss"))))

  (define (winscheme-apps-dir)
    (winscheme-raw-user-apps-dir))

  (define (winscheme-app-normalize-name value)
    (cond
      ((symbol? value) (symbol->string value))
      ((string? value) value)
      (else
       (assertion-violation 'apps "app name must be a symbol or string" value))))

  (define (winscheme-app-entry-ref entry key . maybe-default)
    (let ((pair (assq key entry)))
      (if pair
          (cdr pair)
          (if (null? maybe-default)
              (assertion-violation 'app-entry-ref "missing app field" key)
              (car maybe-default)))))

  (define (winscheme-app-path entry)
    (let* ((file-name (winscheme-app-entry-ref entry 'file))
           (local-path (string-append (winscheme-apps-dir) "/" file-name))
           (fallback-path (string-append (winscheme-raw-examples-dir) "/" file-name)))
      (if (file-exists? local-path) local-path fallback-path)))

  (define (winscheme-app-find name)
    (let ((wanted (winscheme-app-normalize-name name)))
      (let loop ((rest winscheme-app-registry))
        (cond
          ((null? rest) #f)
          ((string=? wanted (symbol->string (winscheme-app-entry-ref (car rest) 'id)))
           (car rest))
          (else
           (loop (cdr rest)))))))

  (define (refresh-apps)
    (let ((count (winscheme-raw-refresh-apps)))
      (if (< count 0)
          (assertion-violation 'refresh-apps "unable to refresh apps from installed examples")
          (begin
            (winscheme-product-menus-refresh!)
            count))))

  (define (apps)
    (display "Sample apps (local WinScheme apps folder, with built-in fallback):\n")
    (for-each
      (lambda (entry)
        (display "  ")
        (write (winscheme-app-entry-ref entry 'id))
        (display "  ")
        (display (winscheme-app-entry-ref entry 'title ""))
        (display "\n      ")
        (display (winscheme-app-entry-ref entry 'summary ""))
        (display "\n"))
      winscheme-app-registry)
    (display "\nRun one with: (run-app 'user-app-weather-demo)\n")
    (display "Refresh local copies with: (refresh-apps)\n")
    (map (lambda (entry) (winscheme-app-entry-ref entry 'id)) winscheme-app-registry))

  (define (app-help name)
    (let ((entry (winscheme-app-find name)))
      (unless entry
        (assertion-violation 'app-help "unknown app" name))
      (display (winscheme-app-entry-ref entry 'title ""))
      (display "\n")
      (display "  id: ")
      (write (winscheme-app-entry-ref entry 'id))
      (display "\n")
      (display "  summary: ")
      (display (winscheme-app-entry-ref entry 'summary ""))
      (display "\n")
      (display "  file: ")
      (display (winscheme-app-path entry))
      (display "\n")
      (display "  run: (run-app '")
      (display (symbol->string (winscheme-app-entry-ref entry 'id)))
      (display ")\n")
      entry))

  (define (app-source-path name)
    (let ((entry (winscheme-app-find name)))
      (unless entry
        (assertion-violation 'app-source-path "unknown app" name))
      (winscheme-app-path entry)))

  (define (run-app name)
    (let ((entry (winscheme-app-find name)))
      (unless entry
        (assertion-violation 'run-app "unknown app" name))
      (load (winscheme-app-path entry))
      (when (and (procedure? winscheme-user-app-raw-run-host)
                 (not (zero? (winscheme-user-app-raw-run-host))))
        (void))
      (winscheme-app-entry-ref entry 'id)))

  (define (search-apps)
    (apps)))
