(begin
  (define winscheme-user-app-native-raw-scheme-dir
    (foreign-procedure "winscheme_scheme_dir_utf8" () string))

  (define (winscheme-user-app-native-scheme-dir)
    (winscheme-user-app-native-raw-scheme-dir))

  (load (string-append (winscheme-user-app-native-scheme-dir) "\\user-app-ui.ss"))
  (load-shared-object "winscheme_user_app_native.dll")

  (define winscheme-user-app-native-raw-available
    (foreign-procedure "winscheme_user_app_native_available" () integer-64))
  (define winscheme-user-app-native-raw-backend-info
    (foreign-procedure "winscheme_user_app_native_backend_info" () string))
  (define winscheme-user-app-native-raw-publish-json
    (foreign-procedure "winscheme_user_app_native_publish_json" (string) integer-64))
  (define winscheme-user-app-native-raw-patch-json
    (foreign-procedure "winscheme_user_app_native_patch_json" (string) integer-64))
  (define winscheme-user-app-native-raw-run-host
    (foreign-procedure "winscheme_user_app_native_host_run" () integer-64))
  (define winscheme-user-app-native-raw-open-url
    (foreign-procedure "winscheme_user_app_native_open_url" (string) integer-64))
  (define winscheme-user-app-native-raw-clipboard-get
    (foreign-procedure "winscheme_user_app_native_clipboard_get" () string))
  (define winscheme-user-app-native-raw-clipboard-set
    (foreign-procedure "winscheme_user_app_native_clipboard_set" (string) integer-64))
  (define winscheme-user-app-native-raw-choose-open-file
    (foreign-procedure "winscheme_user_app_native_choose_open_file" (string) string))
  (define winscheme-user-app-native-raw-choose-save-file
    (foreign-procedure "winscheme_user_app_native_choose_save_file" (string) string))
  (define winscheme-user-app-native-raw-choose-folder
    (foreign-procedure "winscheme_user_app_native_choose_folder" (string) string))

  (winscheme-doc 'user-app-native-available? "Return #t when the native user-app backend is available.")
  (define (user-app-native-available?)
    (not (zero? (winscheme-user-app-native-raw-available))))

  (winscheme-doc 'user-app-native-backend-info "Return backend information for the native user-app host.")
  (define (user-app-native-backend-info)
    (winscheme-user-app-native-raw-backend-info))

  (winscheme-doc 'user-app-native-open-url! "Open a URL with the native shell integration.")
  (define (user-app-native-open-url! url)
    (not (zero? (winscheme-user-app-native-raw-open-url url))))

  (winscheme-doc 'user-app-native-clipboard-text "Return the current clipboard text from the native backend.")
  (define (user-app-native-clipboard-text)
    (winscheme-user-app-native-raw-clipboard-get))

  (winscheme-doc 'user-app-native-clipboard-set! "Replace the native clipboard text content.")
  (define (user-app-native-clipboard-set! text)
    (not (zero? (winscheme-user-app-native-raw-clipboard-set text))))

  (winscheme-doc 'user-app-native-open-file-dialog "Show the native open-file dialog and return the chosen path or #f.")
  (define (user-app-native-open-file-dialog . maybe-initial-path)
    (let ((path (winscheme-user-app-native-raw-choose-open-file
                  (if (null? maybe-initial-path) "" (car maybe-initial-path)))))
      (if (string=? path "") #f path)))

  (winscheme-doc 'user-app-native-save-file-dialog "Show the native save-file dialog and return the chosen path or #f.")
  (define (user-app-native-save-file-dialog . maybe-initial-path)
    (let ((path (winscheme-user-app-native-raw-choose-save-file
                  (if (null? maybe-initial-path) "" (car maybe-initial-path)))))
      (if (string=? path "") #f path)))

  (winscheme-doc 'user-app-native-open-folder-dialog "Show the native folder picker and return the chosen path or #f.")
  (define (user-app-native-open-folder-dialog . maybe-title)
    (let ((chosen (winscheme-user-app-native-raw-choose-folder
                    (if (null? maybe-title) "" (car maybe-title)))))
      (if (or (not chosen) (string=? chosen ""))
          #f
          chosen)))

  ;; Retained normalized tree from the last successful native publish.
  ;; Used by the reconciler to diff against the next render and emit
  ;; targeted patch operations instead of always doing a full republish.
  (define user-app-native-last-rendered-spec #f)

  ;; Set to #t when user-app-native-rerender! is called inside an event
  ;; handler (event-depth > 0).  The post-event hook drains this flag and
  ;; runs the reconciler exactly once after the handler returns.
  (define user-app-native-rerender-pending? #f)

  ;; Reconcile the next rendered tree against the last known native spec.
  ;; - If no prior spec is available, publish the full tree.
  ;; - If the shape is identical, emit only changed-prop patch operations.
  ;; - If the shape differs beyond what the first-pass reconciler can express,
  ;;   fall back to a full publish.
  (winscheme-doc 'user-app-native-run-reconcile! "Reconcile the current render tree against the last native render and publish changes.")
  (define (user-app-native-run-reconcile!)
    (and user-app-render-thunk
         (let ((prepared (user-app-normalized-tree (user-app-render-thunk))))
           (cond
             ((not user-app-native-last-rendered-spec)
              (let ((ok (not (zero? (winscheme-user-app-native-raw-publish-json
                                      (user-app-json-text prepared))))))
                (set! user-app-native-last-rendered-spec (if ok prepared #f))
                ok))
             (else
              (let ((ops (user-app-normalized-tree-diff
                            user-app-native-last-rendered-spec prepared)))
                (cond
                  ;; Shape changed beyond reconciler scope — full publish.
                  ((not ops)
                   (let ((ok (not (zero? (winscheme-user-app-native-raw-publish-json
                                           (user-app-json-text prepared))))))
                     (set! user-app-native-last-rendered-spec (if ok prepared #f))
                     ok))
                  ;; No prop changes at all — update cached spec only.
                  ((null? ops)
                   (set! user-app-native-last-rendered-spec prepared)
                   #t)
                  ;; Targeted patch operations available.
                  (else
                   (let ((ok (not (zero? (winscheme-user-app-native-raw-patch-json
                                           (user-app-json-text
                                             (user-app-normalized-patch-document ops)))))))
                     (set! user-app-native-last-rendered-spec (if ok prepared #f))
                     ok)))))))))

  ;; Direct publish of a given spec, bypassing the reconciler.
  ;; Resets the reconciler cache so the next rerender starts fresh.
  (winscheme-doc 'user-app-native-show! "Publish a complete native UI spec immediately, bypassing the reconciler.")
  (define (user-app-native-show! spec)
    (winscheme-user-app-load-json!)
    (let ((ok (not (zero? (winscheme-user-app-native-raw-publish-json (user-app->json spec))))))
      (set! user-app-native-last-rendered-spec #f)
      ok))

  ;; Direct imperative patch, for app-driven operations such as
  ;; user-app-remove-child! and user-app-prepend-text-child!.
  ;; These use explicit app-assigned IDs and bypass the reconciler.
  (winscheme-doc 'user-app-native-patch! "Apply a native UI patch document directly without republishing the full tree.")
  (define (user-app-native-patch! patch)
    (winscheme-user-app-load-json!)
    (not (zero? (winscheme-user-app-native-raw-patch-json
                  (json->string (user-app-normalize patch))))))

  ;; Rerender using the reconciler. Prefer patch operations over full publish.
  ;; If called inside an event handler (event-depth > 0), defer the actual
  ;; reconcile until the handler returns so that multiple state mutations
  ;; in one handler produce a single patch round-trip, not one per mutation.
  (winscheme-doc 'user-app-native-rerender! "Rerender the native UI using reconciler-driven patching when possible.")
  (define (user-app-native-rerender!)
    (winscheme-user-app-load-json!)
    (if (> user-app-event-depth 0)
        (begin
          (set! user-app-native-rerender-pending? #t)
          #t)
        (user-app-native-run-reconcile!)))

  ;; Post-event hook: flush any pending native rerender once the handler
  ;; has fully returned and event-depth is back to zero.
  (user-app-register-post-event-hook!
    (lambda ()
      (when user-app-native-rerender-pending?
        (set! user-app-native-rerender-pending? #f)
        (user-app-native-run-reconcile!))))

  ;; Start a render loop for the native backend, registering the render
  ;; thunk and optional event handler.  Clears the reconciler cache so
  ;; the first render is always a clean full publish.
  (winscheme-doc 'user-app-native-start! "Register a native render thunk and optional event handler without entering the host loop.")
  (define (user-app-native-start! render . maybe-handler)
    (unless (procedure? render)
      (user-app-fail 'user-app-native-start! render))
    (set! user-app-native-last-rendered-spec #f)
    (set! user-app-render-thunk render)
    (if (null? maybe-handler)
        (user-app-clear-event-handler!)
        (user-app-on-event! (car maybe-handler)))
    (user-app-native-rerender!))

  ;; Like user-app-native-start! but also enters the native host message loop.
  (winscheme-doc 'user-app-native-run-start! "Register the native render thunk and enter the native host message loop.")
  (define (user-app-native-run-start! render . maybe-handler)
    (unless (procedure? render)
      (user-app-fail 'user-app-native-run-start! render))
    (set! user-app-native-last-rendered-spec #f)
    (set! user-app-render-thunk render)
    (if (null? maybe-handler)
        (user-app-clear-event-handler!)
        (user-app-on-event! (car maybe-handler)))
    (and (user-app-native-rerender!)
         (not (zero? (winscheme-user-app-native-raw-run-host)))))

  ;; One-shot: publish a single spec and enter the host loop.
  ;; No render thunk is registered; this is for static views.
  (winscheme-doc 'user-app-native-run! "Publish one native UI spec and enter the host message loop for a static view.")
  (define (user-app-native-run! spec)
    (and (user-app-native-show! spec)
         (not (zero? (winscheme-user-app-native-raw-run-host)))))
)
