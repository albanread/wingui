(begin
  (define winscheme-user-app-raw-scheme-dir
    (foreign-procedure "winscheme_scheme_dir_utf8" () string))
  (define winscheme-user-app-raw-state-dir
    (foreign-procedure "winscheme_state_dir_utf8" () string))
  (define winscheme-user-app-raw-publish-json
    (foreign-procedure "winscheme_user_app_publish_json" (string) integer-64))
  (define winscheme-user-app-raw-patch-json
    (foreign-procedure "winscheme_user_app_patch_json" (string) integer-64))
  (define winscheme-user-app-raw-run-host
    (foreign-procedure "winscheme_user_app_host_run" () integer-64))
  (define winscheme-user-app-raw-open-url
    (foreign-procedure "winscheme_user_app_open_url" (string) integer-64))
  (define winscheme-user-app-raw-clipboard-get
    (foreign-procedure "winscheme_user_app_clipboard_get" () string))
  (define winscheme-user-app-raw-clipboard-set
    (foreign-procedure "winscheme_user_app_clipboard_set" (string) integer-64))
  (define winscheme-user-app-raw-choose-open-file
    (foreign-procedure "winscheme_user_app_choose_open_file" (string) string))
  (define winscheme-user-app-raw-choose-save-file
    (foreign-procedure "winscheme_user_app_choose_save_file" (string) string))
  (define winscheme-user-app-raw-choose-folder
    (foreign-procedure "winscheme_user_app_choose_folder" (string) string))
  (define winscheme-user-app-raw-default-backend
    (foreign-procedure "winscheme_user_app_default_backend_utf8" () string))
  (define winscheme-user-app-raw-set-default-backend
    (foreign-procedure "winscheme_user_app_set_default_backend" (string) integer-64))
  (define winscheme-user-app-raw-last-error
    (foreign-procedure "winscheme_user_app_last_error_utf8" () string))

  (define user-app-json-loaded? #f)
  (define user-app-event-handler #f)
  (define user-app-last-event-value #f)
  (define user-app-render-thunk #f)
  (define user-app-last-rendered-spec #f)
  (define user-app-event-depth 0)
  (define user-app-rerender-pending? #f)
  ;; Thunks registered by backend modules (e.g. native) that should be
  ;; called once at event-depth zero after each handler completes.
  ;; Each thunk is responsible for checking its own pending flag and
  ;; running its reconciler if needed.
  (define user-app-post-event-hooks '())

  (define (user-app-register-post-event-hook! thunk)
    (unless (procedure? thunk)
      (user-app-fail 'user-app-register-post-event-hook! thunk))
    (set! user-app-post-event-hooks
          (cons thunk user-app-post-event-hooks)))

  (define (user-app-state-key key)
    (cond
      ((symbol? key) (symbol->string key))
      ((string? key) key)
      (else (user-app-fail 'user-app-state-key key))))

  (winscheme-doc 'user-app-state "Create or access a reactive UI state object.")
  (define (user-app-state . rest)
    (vector (map (lambda (entry)
                   (cons (user-app-state-key (car entry)) (cdr entry)))
                 (user-app-plist->object rest))))

  (define (user-app-state-entries state)
    (unless (and (vector? state) (= (vector-length state) 1) (list? (vector-ref state 0)))
      (user-app-fail 'user-app-state-entries state))
    (vector-ref state 0))

  (winscheme-doc 'user-app-state-ref "Read a value from a UI state object.")
  (define (user-app-state-ref state key . maybe-default)
    (let* ((normalized-key (user-app-state-key key))
           (entry (assoc normalized-key (user-app-state-entries state))))
      (if entry
          (cdr entry)
          (if (null? maybe-default)
              (user-app-fail 'user-app-state-ref normalized-key)
              (car maybe-default)))))

  (winscheme-doc 'user-app-state-set! "Write a value to a UI state object.")
  (define (user-app-state-set! state key value)
    (let* ((normalized-key (user-app-state-key key))
           (entries (user-app-state-entries state))
           (entry (assoc normalized-key entries)))
      (if entry
          (set-cdr! entry value)
          (vector-set! state 0 (cons (cons normalized-key value) entries)))
      value))

  (winscheme-doc 'user-app-state-update! "Mutate a value in a UI state object using a callback.")
  (define (user-app-state-update! state key updater . maybe-default)
    (unless (procedure? updater)
      (user-app-fail 'user-app-state-update! updater))
    (let* ((current (apply user-app-state-ref state key maybe-default))
           (updated (updater current)))
      (user-app-state-set! state key updated)))

  (define (user-app-state-merge! state . rest)
    (for-each (lambda (entry)
                (user-app-state-set! state (car entry) (cdr entry)))
              (user-app-plist->object rest))
    state)

  (winscheme-doc 'user-app-state->object "Convert a reactive UI state object into a plain object/alist.")
  (define (user-app-state->object state)
    (map (lambda (entry)
           (cons (car entry) (cdr entry)))
         (user-app-state-entries state)))

  (define (winscheme-user-app-scheme-dir)
    (winscheme-user-app-raw-scheme-dir))

  (define (winscheme-user-app-state-dir)
    (winscheme-user-app-raw-state-dir))

  (define (user-app-normalize-backend backend)
    (cond
      ((symbol? backend)
       (user-app-normalize-backend (symbol->string backend)))
      ((string? backend)
       (if (string-ci=? backend "native") "native" "web"))
      (else
       (user-app-fail 'user-app-normalize-backend backend))))

  (winscheme-doc 'user-app-backend "Get the configured UI rendering backend (e.g. 'native' or 'web')." 'prim #t)
  (define (user-app-backend)
    (let ((backend (winscheme-user-app-raw-default-backend)))
      (user-app-normalize-backend (string-append backend ""))))

  (winscheme-doc 'user-app-set-backend! "Set the desired UI rendering backend." 'prim #t)
  (define (user-app-set-backend! backend)
    (let ((normalized (user-app-normalize-backend backend)))
      (winscheme-user-app-raw-set-default-backend normalized)
      normalized))

  (define (winscheme-user-app-load-json!)
    (unless user-app-json-loaded?
      (load (string-append (winscheme-user-app-scheme-dir) "/json.ss"))
      (set! user-app-json-loaded? #t)))

  (define (user-app-fail who . irritants)
    (apply assertion-violation who "invalid user-app declarative value" irritants))

  (define (user-app-last-error)
    (let ((message (winscheme-user-app-raw-last-error)))
      (if (or (not message) (string=? message ""))
          #f
          message)))

  (define (user-app-ensure-success who ok payload)
    (if ok
        #t
        (assertion-violation
          who
          (or (user-app-last-error) "user-app operation failed")
          payload)))

  (define (user-app-object-list? value)
    (and (list? value)
         (let loop ((rest value))
           (or (null? rest)
               (and (pair? (car rest))
                    (or (string? (caar rest)) (symbol? (caar rest)))
                    (loop (cdr rest)))))))

  (define (user-app-normalize value)
    (cond
      ((or (string? value) (number? value) (boolean? value))
       value)
      ((symbol? value)
       (symbol->string value))
      ((vector? value)
       (list->vector (map user-app-normalize (vector->list value))))
      ((user-app-object-list? value)
       (map (lambda (entry)
              (cons (if (symbol? (car entry)) (symbol->string (car entry)) (car entry))
                    (user-app-normalize (cdr entry))))
            value))
      ((list? value)
       (map user-app-normalize value))
      (else
       (user-app-fail 'user-app-normalize value))))

  (define (user-app-object-ref object key default)
    (let* ((normalized-key (if (symbol? key) (symbol->string key) key))
           (entry (and (user-app-object-list? object) (assoc normalized-key object))))
      (if entry (cdr entry) default)))

  (define (user-app-object-set object key value)
    (let ((normalized-key (if (symbol? key) (symbol->string key) key)))
      (let loop ((remaining object) (result '()) (updated? #f))
        (cond
          ((null? remaining)
           (reverse (if updated?
                        result
                        (cons (cons normalized-key value) result))))
          ((string=? (car (car remaining)) normalized-key)
           (loop (cdr remaining) (cons (cons normalized-key value) result) #t))
          (else
           (loop (cdr remaining) (cons (car remaining) result) updated?))))))

  (define (user-app-node-id node)
    (user-app-object-ref node "id" ""))

  (define (user-app-union-keys left right)
    (let loop ((remaining (append (map car left) (map car right))) (seen '()) (result '()))
      (cond
        ((null? remaining) (reverse result))
        ((member (car remaining) seen)
         (loop (cdr remaining) seen result))
        (else
         (loop (cdr remaining)
               (cons (car remaining) seen)
               (cons (car remaining) result))))))

  (define (user-app-scalar-value? value)
    (or (string? value)
        (number? value)
        (boolean? value)
        (not value)))

  (define (user-app-structured-prop-patchable? node-type key old-value new-value)
    (cond
      ((and (string=? node-type "table")
            (string=? key "rows")
            (list? old-value)
            (list? new-value))
       #t)
      ((and (string=? node-type "tree-view")
            (or (string=? key "items")
                (string=? key "expandedIds"))
            (list? old-value)
            (list? new-value))
       #t)
      ((and (string=? node-type "context-menu")
            (string=? key "items")
            (list? old-value)
            (list? new-value))
       #t)
      ((and (string=? node-type "canvas")
            (string=? key "commands")
            (list? old-value)
            (list? new-value))
       #t)
      (else
       #f)))

  (define (user-app-normalized-tree spec)
    (letrec
      ((walk
         (lambda (value path)
           (cond
             ((user-app-object-list? value)
              (let* ((mapped
                       (map (lambda (entry)
                              (let* ((key (car entry))
                                     (child-path (string-append path "/" key)))
                                (cons key (walk (cdr entry) child-path))))
                            value))
                     (type (user-app-object-ref mapped "type" #f)))
                (if type
                    (let ((node-id (user-app-object-ref mapped "id" #f)))
                      (if node-id
                          mapped
                          (append mapped (list (cons "id" (string-append "__auto__:" path))))))
                    mapped)))
             ((vector? value)
              (list->vector
                (let loop ((index 0) (items (vector->list value)))
                  (if (null? items)
                      '()
                      (cons (walk (car items)
                                  (string-append path "/" (number->string index)))
                            (loop (+ index 1) (cdr items)))))))
             ((list? value)
              (let loop ((index 0) (items value))
                (if (null? items)
                    '()
                    (cons (walk (car items)
                                (string-append path "/" (number->string index)))
                          (loop (+ index 1) (cdr items))))))
             (else
              value)))))
      (walk (user-app-normalize spec) "window")))

  (define (user-app-normalized-node-prop-diff old-node new-node)
    (let ((node-type (user-app-object-ref new-node "type" "")))
      (let loop ((keys (user-app-union-keys old-node new-node)) (changed '()))
      (cond
        ((null? keys) (reverse changed))
        ((member (car keys) '("type" "id" "children" "body"))
         (loop (cdr keys) changed))
        (else
         (let* ((key (car keys))
                (old-value (user-app-object-ref old-node key #f))
                (new-value (user-app-object-ref new-node key #f)))
           (cond
             ((equal? old-value new-value)
              (loop (cdr keys) changed))
             ((and (user-app-scalar-value? old-value)
                   (user-app-scalar-value? new-value))
              (loop (cdr keys) (cons (cons key new-value) changed)))
             ((user-app-structured-prop-patchable? node-type key old-value new-value)
              (loop (cdr keys) (cons (cons key new-value) changed)))
             (else
              #f))))))))

  (define (user-app-normalized-children-all-identifiable? children)
    (and (list? children)
         (let loop ((remaining children))
           (or (null? remaining)
               (let ((child (car remaining)))
                 (and (user-app-object-list? child)
                      (let ((node-id (user-app-node-id child)))
                        (and (string? node-id)
                             (not (string=? node-id ""))
                             (loop (cdr remaining))))))))))

  (define (user-app-normalized-child-id-list children)
    (map user-app-node-id children))

  (define (user-app-list-filter predicate items)
    (let loop ((rest items) (result '()))
      (cond
        ((null? rest) (reverse result))
        ((predicate (car rest))
         (loop (cdr rest) (cons (car rest) result)))
        (else
         (loop (cdr rest) result)))))

  (define (user-app-find-child-by-id children child-id)
    (let loop ((rest children))
      (if (null? rest)
          #f
          (let ((child (car rest)))
            (if (equal? (user-app-node-id child) child-id)
                child
                (loop (cdr rest)))))))

  (define (user-app-list-remove-first items value)
    (let loop ((remaining items) (result '()) (removed? #f))
      (cond
        ((null? remaining) (reverse result))
        ((and (not removed?) (equal? (car remaining) value))
         (loop (cdr remaining) result #t))
        (else
         (loop (cdr remaining) (cons (car remaining) result) removed?)))))

  (define (user-app-list-insert-at items index value)
    (let loop ((remaining items) (position 0) (result '()) (inserted? #f))
      (cond
        ((null? remaining)
         (reverse (if inserted? result (cons value result))))
        ((and (not inserted?) (<= index position))
         (loop remaining position (cons value result) #t))
        (else
         (loop (cdr remaining)
               (+ position 1)
               (cons (car remaining) result)
               inserted?)))))

  (define (user-app-normalized-children-order-preserved? old-ids new-ids)
    (equal?
      (user-app-list-filter (lambda (child-id) (member child-id new-ids)) old-ids)
      (user-app-list-filter (lambda (child-id) (member child-id old-ids)) new-ids)))

  (define (user-app-normalized-children-diff parent-id old-children new-children)
    (cond
      ((and (= (length old-children) (length new-children))
            (equal? (user-app-normalized-child-id-list old-children)
                    (user-app-normalized-child-id-list new-children)))
       (let loop ((old-rest old-children) (new-rest new-children) (ops '()))
         (if (null? old-rest)
             (reverse ops)
             (let ((child-ops (user-app-normalized-node-diff (car old-rest) (car new-rest))))
               (if (not child-ops)
                   #f
                   (loop (cdr old-rest) (cdr new-rest) (append (reverse child-ops) ops)))))))
      ((and parent-id
            (not (string=? parent-id ""))
            (user-app-normalized-children-all-identifiable? old-children)
            (user-app-normalized-children-all-identifiable? new-children))
       (let* ((old-ids (user-app-normalized-child-id-list old-children))
              (new-ids (user-app-normalized-child-id-list new-children))
              (shared-old-ids
                (user-app-list-filter
                  (lambda (child-id) (member child-id new-ids))
                  old-ids))
              (shared-new-ids
                (user-app-list-filter
                  (lambda (child-id) (member child-id old-ids))
                  new-ids))
              (replace-ops
                (list
                  (list
                    (cons 'op "replace-children")
                    (cons 'id parent-id)
                    (cons 'children new-children))))
               (shared-ops
                 (let loop ((remaining shared-new-ids) (ops '()))
                   (if (null? remaining)
                       (reverse ops)
                       (let* ((child-id (car remaining))
                              (old-child (user-app-find-child-by-id old-children child-id))
                              (new-child (user-app-find-child-by-id new-children child-id))
                              (child-ops
                                (and old-child
                                     new-child
                                     (user-app-normalized-node-diff old-child new-child))))
                         (if (not child-ops)
                             #f
                             (loop (cdr remaining)
                                   (append (reverse child-ops) ops)))))))
               (remove-ops
                 (map
                   (lambda (child)
                     (list
                       (cons 'op "remove-child")
                       (cons 'id parent-id)
                       (cons 'childId (user-app-node-id child))))
                   (user-app-list-filter
                     (lambda (child)
                       (not (member (user-app-node-id child) new-ids)))
                     old-children)))
               (reorder-ops
                 (let loop ((index 0)
                            (current-order shared-old-ids)
                            (remaining new-children)
                            (ops '()))
                   (if (null? remaining)
                       (reverse ops)
                       (let* ((child (car remaining))
                              (child-id (user-app-node-id child)))
                         (cond
                           ((member child-id old-ids)
                            (if (and (< index (length current-order))
                                     (equal? (list-ref current-order index) child-id))
                                (loop (+ index 1) current-order (cdr remaining) ops)
                                (loop
                                  (+ index 1)
                                  (user-app-list-insert-at
                                    (user-app-list-remove-first current-order child-id)
                                    index
                                    child-id)
                                  (cdr remaining)
                                  (cons
                                    (list
                                      (cons 'op "move-child")
                                      (cons 'id parent-id)
                                      (cons 'childId child-id)
                                      (cons 'index index))
                                    ops))))
                           (else
                            (let ((append? (= index (length current-order))))
                              (loop (+ index 1)
                                    (user-app-list-insert-at current-order index child-id)
                                    (cdr remaining)
                                    (cons
                                      (append
                                        (list
                                          (cons 'op (if append? "append-child" "insert-child"))
                                          (cons 'id parent-id))
                                        (if append?
                                            '()
                                            (list (cons 'index index)))
                                        (list (cons 'child child)))
                                      ops))))))))))
         (if (not shared-ops)
             replace-ops
             (append remove-ops reorder-ops shared-ops))))
      (else
       #f)))

  (define (user-app-normalized-node-diff old-node new-node)
    (if (or (not (user-app-object-list? old-node))
            (not (user-app-object-list? new-node))
            (not (equal? (user-app-object-ref old-node "type" #f)
                         (user-app-object-ref new-node "type" #f)))
            (not (equal? (user-app-object-ref old-node "id" #f)
                         (user-app-object-ref new-node "id" #f))))
        #f
        (let ((prop-changes (user-app-normalized-node-prop-diff old-node new-node)))
          (if (not prop-changes)
              #f
              (let* ((body-old (user-app-object-ref old-node "body" #f))
                     (body-new (user-app-object-ref new-node "body" #f))
                     (body-ops
                       (cond
                         ((and body-old body-new)
                          (user-app-normalized-node-diff body-old body-new))
                         ((and (not body-old) (not body-new))
                          '())
                         (else #f)))
                     (children-old (user-app-object-ref old-node "children" '()))
                     (children-new (user-app-object-ref new-node "children" '()))
                     (children-ops
                       (if (and (list? children-old) (list? children-new))
                           (user-app-normalized-children-diff (user-app-node-id new-node) children-old children-new)
                           (if (equal? children-old children-new) '() #f))))
                (if (or (not body-ops) (not children-ops))
                    #f
                    (append
                      (if (null? prop-changes)
                          '()
                          (list
                            (list
                              (cons 'op "set-node-props")
                              (cons 'id (user-app-object-ref new-node "id" ""))
                              (cons 'props prop-changes))))
                      body-ops
                      children-ops)))))))

  (define (user-app-normalized-tree-diff old-spec new-spec)
    (if (or (not (user-app-object-list? old-spec))
            (not (user-app-object-list? new-spec))
            (not (equal? (user-app-object-ref old-spec "type" #f) "window"))
            (not (equal? (user-app-object-ref new-spec "type" #f) "window")))
        #f
        (let ((window-prop-changes
                (let loop ((keys (user-app-union-keys old-spec new-spec)) (changed '()))
                  (cond
                    ((null? keys) (reverse changed))
                    ((member (car keys) '("type" "id" "body"))
                     (loop (cdr keys) changed))
                    (else
                     (let* ((key (car keys))
                            (old-value (user-app-object-ref old-spec key #f))
                            (new-value (user-app-object-ref new-spec key #f)))
                       (cond
                         ((equal? old-value new-value)
                          (loop (cdr keys) changed))
                         ((and (user-app-scalar-value? old-value)
                               (user-app-scalar-value? new-value))
                          (loop (cdr keys) (cons (cons key new-value) changed)))
                         (else
                          #f)))))))
              (body-ops
                (user-app-normalized-node-diff
                  (user-app-object-ref old-spec "body" #f)
                  (user-app-object-ref new-spec "body" #f))))
          (if (or (not window-prop-changes) (not body-ops))
              #f
              (append
                (if (null? window-prop-changes)
                    '()
                    (list
                      (list
                        (cons 'op "set-window-props")
                        (cons 'props window-prop-changes))))
                body-ops)))))

  (define (user-app-normalized-patch-document ops)
    (user-app-normalize (list (cons 'ops ops))))

  (define (user-app-plist->object rest)
    (let loop ((remaining rest) (entries '()))
      (cond
        ((null? remaining)
         (reverse entries))
        ((null? (cdr remaining))
         (user-app-fail 'user-app-plist->object remaining))
        (else
         (let ((key (car remaining))
               (value (cadr remaining)))
           (unless (or (symbol? key) (string? key))
             (user-app-fail 'user-app-plist->object key))
           (loop (cddr remaining) (cons (cons key value) entries)))))))

  (define (user-app-node type props children)
    (append
      (list (cons 'type type))
      props
      (if (null? children) '() (list (cons 'children children)))))

  (winscheme-doc 'user-app-stack "Create a vertical stack layout container.")
  (define (user-app-stack . children)
    (user-app-node "stack" '() children))

  (winscheme-doc 'user-app-row "Create a horizontal row layout container.")
  (define (user-app-row . children)
    (user-app-node "row" '() children))

  (winscheme-doc 'user-app-toolbar "Create a distinct toolbar layout container.")
  (define (user-app-toolbar . children)
    (user-app-node "toolbar" '() children))

  (winscheme-doc 'user-app-menu-item "Define a clickable action within a menu.")
  (define (user-app-menu-item text event . rest)
    (append
      (list
        (cons 'text text)
        (cons 'event (if (symbol? event) (symbol->string event) event)))
      (user-app-plist->object rest)))

  (winscheme-doc 'user-app-menu "Create a dropdown menu for a menu bar.")
  (define (user-app-menu text items . rest)
    (append
      (list
        (cons 'text text)
        (cons 'items items))
      (user-app-plist->object rest)))

  (winscheme-doc 'user-app-menu-bar "Create a native application menu bar.")
  (define (user-app-menu-bar . menus)
    (user-app-node "menu-bar" '() menus))

  (winscheme-doc 'user-app-context-menu "Wrap content with a right-click context menu.")
  (define (user-app-context-menu items content . rest)
    (user-app-node
      "context-menu"
      (append
        (list (cons 'items items))
        (user-app-plist->object rest))
      (list content)))

  (winscheme-doc 'user-app-split-pane "Define a panel inside a split view.")
  (define (user-app-split-pane id . children-and-rest)
    (let loop ((remaining children-and-rest) (children '()) (props '()))
      (cond
        ((null? remaining)
         (user-app-node
           "split-pane"
           (append
             (list (cons 'id (if (symbol? id) (symbol->string id) id)))
             (reverse props))
           (reverse children)))
        ((and (pair? remaining)
              (or (symbol? (car remaining)) (string? (car remaining))))
         (if (null? (cdr remaining))
             (user-app-fail 'user-app-split-pane remaining)
             (loop (cddr remaining)
                   children
                   (cons (cons (car remaining) (cadr remaining)) props))))
        (else
         (loop (cdr remaining)
               (cons (car remaining) children)
               props)))))

  (winscheme-doc 'user-app-split-view "Create a resizable split view container (horizontal or vertical).")
  (define (user-app-split-view orientation event . panes-and-rest)
    (let loop ((remaining panes-and-rest) (panes '()) (props '()))
      (cond
        ((null? remaining)
         (user-app-node
           "split-view"
           (append
             (list
               (cons 'orientation (if (symbol? orientation) (symbol->string orientation) orientation))
               (cons 'event (if (symbol? event) (symbol->string event) event)))
             (reverse props))
           (reverse panes)))
        ((and (pair? remaining)
              (or (symbol? (car remaining)) (string? (car remaining))))
         (if (null? (cdr remaining))
             (user-app-fail 'user-app-split-view remaining)
             (loop (cddr remaining)
                   panes
                   (cons (cons (car remaining) (cadr remaining)) props))))
        (else
         (loop (cdr remaining)
               (cons (car remaining) panes)
               props)))))

  (winscheme-doc 'user-app-text "Create a standard textual label or paragraph.")
  (define (user-app-text text . rest)
    (user-app-node "text" (append (list (cons 'text text)) (user-app-plist->object rest)) '()))

  (winscheme-doc 'user-app-heading "Create a prominent heading text component.")
  (define (user-app-heading text . rest)
    (user-app-node "heading" (append (list (cons 'text text)) (user-app-plist->object rest)) '()))

  (winscheme-doc 'user-app-link "Create a clickable hyperlink.")
  (define (user-app-link text href event . rest)
    (user-app-node
      "link"
      (append
        (list
          (cons 'text text)
          (cons 'href href)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-image "Create an image component from a source path/URL.")
  (define (user-app-image src alt . rest)
    (user-app-node
      "image"
      (append
        (list
          (cons 'src src)
          (cons 'alt alt))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-card "Create an elevated card container with a title.")
  (define (user-app-card title . children)
    (user-app-node "card" (list (cons 'title title)) children))

  (winscheme-doc 'user-app-divider "Create a visual separator line.")
  (define (user-app-divider . rest)
    (user-app-node "divider" (user-app-plist->object rest) '()))

  (winscheme-doc 'user-app-button "Create a clickable button component.")
  (define (user-app-button text event . rest)
    (user-app-node
      "button"
      (append
        (list (cons 'text text) (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-input "Create a single-line text input field.")
  (define (user-app-input label value event . rest)
    (user-app-node
      "input"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-number-input "Create a numeric spinner input field.")
  (define (user-app-number-input label value event . rest)
    (user-app-node
      "number-input"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-date-picker "Create a date selection input.")
  (define (user-app-date-picker label value event . rest)
    (user-app-node
      "date-picker"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-time-picker "Create a time selection input.")
  (define (user-app-time-picker label value event . rest)
    (user-app-node
      "time-picker"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-textarea "Create a multi-line text input field.")
  (define (user-app-textarea label value event . rest)
    (user-app-node
      "textarea"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-checkbox "Create a binary checkbox input.")
  (define (user-app-checkbox label checked event . rest)
    (user-app-node
      "checkbox"
      (append
        (list
          (cons 'label label)
          (cons 'checked (and checked #t))
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-switch "Create a binary toggle switch input.")
  (define (user-app-switch label checked event . rest)
    (user-app-node
      "switch"
      (append
        (list
          (cons 'label label)
          (cons 'checked (and checked #t))
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-option "Define a selectable option for select, list-box, or radio-group controls.")
  (define (user-app-option value text . rest)
    (append
      (list
        (cons 'value value)
        (cons 'text text))
      (user-app-plist->object rest)))

  (winscheme-doc 'user-app-column "Configure a column definition for a table.")
  (define (user-app-column key title . rest)
    (append
      (list
        (cons 'key (if (symbol? key) (symbol->string key) key))
        (cons 'title title))
      (user-app-plist->object rest)))

  (winscheme-doc 'user-app-table-row "Define a row within a data table.")
  (define (user-app-table-row id . rest)
    (append
      (list (cons 'id id))
      (user-app-plist->object rest)))

  (winscheme-doc 'user-app-select "Create a dropdown select input.")
  (define (user-app-select label value event options . rest)
    (user-app-node
      "select"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event))
          (cons 'options options))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-table "Create a data table grid.")
  (define (user-app-table label columns rows event . rest)
    (user-app-node
      "table"
      (append
        (list
          (cons 'label label)
          (cons 'columns columns)
          (cons 'rows rows)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-tree-item "Define an item within a tree view.")
  (define (user-app-tree-item id text . rest)
    (append
      (list
        (cons 'id (if (symbol? id) (symbol->string id) id))
        (cons 'text text))
      (user-app-plist->object rest)))

  (winscheme-doc 'user-app-tree-view "Create a hierarchical, expandable tree control.")
  (define (user-app-tree-view label items event . rest)
    (user-app-node
      "tree-view"
      (append
        (list
          (cons 'label label)
          (cons 'items items)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-list-box "Create a list-box selector control.")
  (define (user-app-list-box label value event options . rest)
    (user-app-node
      "list-box"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event))
          (cons 'options options))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-tab "Define an individual tab panel.")
  (define (user-app-tab value text content . rest)
    (append
      (list
        (cons 'value value)
        (cons 'text text)
        (cons 'content content))
      (user-app-plist->object rest)))

  (winscheme-doc 'user-app-tabs "Create a tabbed navigation container.")
  (define (user-app-tabs label value event tabs . rest)
    (user-app-node
      "tabs"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event))
          (cons 'tabs tabs))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-radio-group "Create a mutually exclusive radio-button group.")
  (define (user-app-radio-group label value event options . rest)
    (user-app-node
      "radio-group"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event))
          (cons 'options options))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-slider "Create a range slider input.")
  (define (user-app-slider label value event . rest)
    (user-app-node
      "slider"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-progress "Create a progress bar indicator.")
  (define (user-app-progress label value . rest)
    (user-app-node
      "progress"
      (append
        (list
          (cons 'label label)
          (cons 'value value))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-badge "Create a small notification badge or chip.")
  (define (user-app-badge text . rest)
    (user-app-node
      "badge"
      (append (list (cons 'text text)) (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-rich-text "Create a rich-text editor field.")
  (define (user-app-rich-text label value event . rest)
    (user-app-node
      "rich-text"
      (append
        (list
          (cons 'label label)
          (cons 'value value)
          (cons 'event (if (symbol? event) (symbol->string event) event)))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-canvas "Create a custom 2D drawing canvas.")
  (define (user-app-canvas width height commands . rest)
    (user-app-node
      "canvas"
      (append
        (list
          (cons 'width width)
          (cons 'height height)
          (cons 'commands commands))
        (user-app-plist->object rest))
      '()))

  (winscheme-doc 'user-app-window "Define the top-level window properties (e.g. title) wrapping the UI body.")
  (define (user-app-window title body . rest)
    (append
      (list
        (cons 'type "window")
        (cons 'title title)
        (cons 'body body))
      (user-app-plist->object rest)))

  (define (user-app-json-text value)
    (winscheme-user-app-load-json!)
    (json->string value))

  (define (user-app-run-reconcile!)
    (and user-app-render-thunk
         (let ((prepared (user-app-normalized-tree (user-app-render-thunk))))
           (cond
             ((not user-app-last-rendered-spec)
              (let ((ok (not (zero? (winscheme-user-app-raw-publish-json (user-app-json-text prepared))))))
                (if ok
                    (set! user-app-last-rendered-spec prepared)
                    (set! user-app-last-rendered-spec #f))
                (user-app-ensure-success 'user-app-run-reconcile! ok prepared)))
             (else
              (let ((ops (user-app-normalized-tree-diff user-app-last-rendered-spec prepared)))
                (cond
                  ((not ops)
                   (let ((ok (not (zero? (winscheme-user-app-raw-publish-json (user-app-json-text prepared))))))
                     (if ok
                         (set! user-app-last-rendered-spec prepared)
                         (set! user-app-last-rendered-spec #f))
                     (user-app-ensure-success 'user-app-run-reconcile! ok prepared)))
                  ((null? ops)
                   (set! user-app-last-rendered-spec prepared)
                   #t)
                  (else
                   (let ((ok (not (zero? (winscheme-user-app-raw-patch-json
                                           (user-app-json-text
                                             (user-app-normalized-patch-document ops)))))))
                     (if ok
                         (set! user-app-last-rendered-spec prepared)
                         (set! user-app-last-rendered-spec #f))
                     (user-app-ensure-success 'user-app-run-reconcile! ok ops))))))))))

  (define (user-app->json spec)
    (user-app-json-text (user-app-normalized-tree spec)))

  (winscheme-doc 'user-app-event-ref "Extract a specific property value from an event object.")
  (define (user-app-event-ref event key . maybe-default)
    (winscheme-user-app-load-json!)
    (apply json-object-ref event key maybe-default))

  (winscheme-doc 'user-app-event-name "Extract the name identifier of the current event.")
  (define (user-app-event-name event)
    (user-app-event-ref event "event" ""))

  (winscheme-doc 'user-app-on-event! "Register a global callback function to handle UI events.")
  (define (user-app-on-event! handler)
    (set! user-app-event-handler handler)
    handler)

  (winscheme-doc 'user-app-clear-event-handler! "Remove the registered global UI event handler.")
  (define (user-app-clear-event-handler!)
    (set! user-app-event-handler #f)
    (void))

  (winscheme-doc 'user-app-last-event "Return the most recently received UI event object.")
  (define (user-app-last-event)
    user-app-last-event-value)

  (winscheme-doc 'user-app-open-url! "Open a universal resource locator in the default system browser.")
  (define (user-app-open-url! url)
    (not (zero? (winscheme-user-app-raw-open-url url))))

  (winscheme-doc 'user-app-clipboard-text "Get text from the system clipboard.")
  (define (user-app-clipboard-text)
    (winscheme-user-app-raw-clipboard-get))

  (winscheme-doc 'user-app-clipboard-set! "Copy text to the system clipboard.")
  (define (user-app-clipboard-set! text)
    (not (zero? (winscheme-user-app-raw-clipboard-set text))))

  (winscheme-doc 'user-app-open-file-dialog "Prompt the user with a native Open File dialog.")
  (define (user-app-open-file-dialog . maybe-initial-path)
    (let ((path (winscheme-user-app-raw-choose-open-file
                  (if (null? maybe-initial-path) "" (car maybe-initial-path)))))
      (if (string=? path "") #f path)))

  (winscheme-doc 'user-app-save-file-dialog "Prompt the user with a native Save File dialog.")
  (define (user-app-save-file-dialog . maybe-initial-path)
    (let ((path (winscheme-user-app-raw-choose-save-file
                  (if (null? maybe-initial-path) "" (car maybe-initial-path)))))
      (if (string=? path "") #f path)))

  (winscheme-doc 'user-app-rerender! "Mark the UI as dirty and trigger a virtual DOM reconciliation." 'prim #t)
  (define (user-app-rerender!)
    (winscheme-user-app-load-json!)
    (if (> user-app-event-depth 0)
        (begin
          (set! user-app-rerender-pending? #t)
          #t)
        (user-app-run-reconcile!)))

  (winscheme-doc 'user-app-start! "Initialize and begin the reactive UI lifecycle without blocking the main thread." 'prim #t)
  (define (user-app-start! render . maybe-handler)
    (unless (procedure? render)
      (user-app-fail 'user-app-start! render))
    (set! user-app-render-thunk render)
    (if (null? maybe-handler)
        (user-app-clear-event-handler!)
        (user-app-on-event! (car maybe-handler)))
    (user-app-rerender!))

  (winscheme-doc 'user-app-run-start! "Start a reactive UI application and block the main thread waiting for events." 'prim #t)
  (define (user-app-run-start! render . maybe-handler)
    (unless (procedure? render)
      (user-app-fail 'user-app-run-start! render))
    (set! user-app-render-thunk render)
    (if (null? maybe-handler)
        (user-app-clear-event-handler!)
        (user-app-on-event! (car maybe-handler)))
    (and (user-app-rerender!)
         (user-app-ensure-success
           'user-app-run-start!
           (not (zero? (winscheme-user-app-raw-run-host)))
           'run-host)))

  (winscheme-doc 'user-app-show! "Publish a static declarative UI tree to the backend." 'prim #t)
  (define (user-app-show! spec)
    (winscheme-user-app-load-json!)
    (let* ((prepared (user-app-normalized-tree spec))
           (ok (not (zero? (winscheme-user-app-raw-publish-json (user-app-json-text prepared))))))
      (if ok
          (set! user-app-last-rendered-spec prepared)
          (set! user-app-last-rendered-spec #f))
      (user-app-ensure-success 'user-app-show! ok prepared)))

  (winscheme-doc 'user-app-patch! "Send raw imperative patch operations to update the UI efficiently.")
  (define (user-app-patch! patch)
    (winscheme-user-app-load-json!)
    (let ((ok (not (zero? (winscheme-user-app-raw-patch-json (user-app-json-text (user-app-normalize patch)))))))
      (set! user-app-last-rendered-spec #f)
      (user-app-ensure-success 'user-app-patch! ok patch)))

  (winscheme-doc 'user-app-batch-patch! "Send a batch patch document containing multiple imperative UI operations.")
  (define (user-app-batch-patch! ops)
    (user-app-patch! (list (cons 'ops ops))))

  (winscheme-doc 'user-app-set-node-props! "Imperatively update arbitrary properties on a UI node.")
  (define (user-app-set-node-props! node-id . rest)
    (user-app-patch!
      (list
        (cons 'op "set-node-props")
        (cons 'id node-id)
        (cons 'props (user-app-plist->object rest)))))

  (winscheme-doc 'user-app-set-text! "Imperatively update the text content of a UI node.")
  (define (user-app-set-text! node-id text)
    (user-app-set-node-props! node-id 'text text))

  (winscheme-doc 'user-app-set-value! "Imperatively update the input value of a UI node.")
  (define (user-app-set-value! node-id value)
    (user-app-set-node-props! node-id 'value value))

  (winscheme-doc 'user-app-set-checked! "Imperatively toggle a checkbox or switch.")
  (define (user-app-set-checked! node-id checked)
    (user-app-set-node-props! node-id 'checked (and checked #t)))

  (winscheme-doc 'user-app-set-selected-id! "Imperatively update the selected item id of a control such as a table or list.")
  (define (user-app-set-selected-id! node-id selected-id)
    (user-app-set-node-props! node-id 'selectedId selected-id))

  (winscheme-doc 'user-app-tree-set-selected-id! "Imperatively update the selected item id of a tree view.")
  (define (user-app-tree-set-selected-id! node-id selected-id)
    (user-app-set-node-props! node-id 'selectedId selected-id))

  (winscheme-doc 'user-app-tree-set-expanded-ids! "Imperatively replace the expanded item ids of a tree view.")
  (define (user-app-tree-set-expanded-ids! node-id expanded-ids)
    (user-app-set-node-props! node-id 'expandedIds expanded-ids))

  (winscheme-doc 'user-app-tree-replace-items! "Imperatively replace all items in a tree view.")
  (define (user-app-tree-replace-items! node-id items)
    (user-app-patch!
      (list
        (cons 'op "tree-replace-items")
        (cons 'id node-id)
        (cons 'items items))))

  (winscheme-doc 'user-app-tree-insert-item! "Imperatively insert a tree item at a given parent and index.")
  (define (user-app-tree-insert-item! node-id parent-id index item)
    (user-app-patch!
      (list
        (cons 'op "tree-insert-item")
        (cons 'id node-id)
        (cons 'parentId (if parent-id parent-id ""))
        (cons 'index index)
        (cons 'item item))))

  (winscheme-doc 'user-app-tree-remove-item! "Imperatively remove a tree item by id.")
  (define (user-app-tree-remove-item! node-id item-id)
    (user-app-patch!
      (list
        (cons 'op "tree-remove-item")
        (cons 'id node-id)
        (cons 'itemId item-id))))

  (winscheme-doc 'user-app-tree-move-item! "Imperatively move a tree item to a new parent and index.")
  (define (user-app-tree-move-item! node-id item-id parent-id index)
    (user-app-patch!
      (list
        (cons 'op "tree-move-item")
        (cons 'id node-id)
        (cons 'itemId item-id)
        (cons 'parentId (if parent-id parent-id ""))
        (cons 'index index))))

  (winscheme-doc 'user-app-tree-set-item-props! "Imperatively update arbitrary properties on a single tree item.")
  (define (user-app-tree-set-item-props! node-id item-id . rest)
    (user-app-patch!
      (list
        (cons 'op "tree-set-item-props")
        (cons 'id node-id)
        (cons 'itemId item-id)
        (cons 'props (user-app-plist->object rest)))))

  (winscheme-doc 'user-app-context-menu-set-items! "Imperatively replace the menu items of a context menu wrapper.")
  (define (user-app-context-menu-set-items! node-id items)
    (user-app-set-node-props! node-id 'items items))

  (winscheme-doc 'user-app-split-view-set-orientation! "Imperatively change a split view orientation between horizontal and vertical.")
  (define (user-app-split-view-set-orientation! node-id orientation)
    (user-app-set-node-props!
      node-id
      'orientation
      (if (symbol? orientation) (symbol->string orientation) orientation)))

  (winscheme-doc 'user-app-split-pane-set-size! "Imperatively update the size of a split pane.")
  (define (user-app-split-pane-set-size! pane-id size)
    (user-app-set-node-props! pane-id 'size size))

  (winscheme-doc 'user-app-split-pane-set-collapsed! "Imperatively collapse or expand a split pane.")
  (define (user-app-split-pane-set-collapsed! pane-id collapsed)
    (user-app-set-node-props! pane-id 'collapsed (and collapsed #t)))

  (winscheme-doc 'user-app-set-window-props! "Imperatively update window state (like title).")
  (define (user-app-set-window-props! . rest)
    (user-app-patch!
      (list
        (cons 'op "set-window-props")
        (cons 'props (user-app-plist->object rest)))))

  (winscheme-doc 'user-app-replace-children! "Imperatively replace all children inside a container node.")
  (define (user-app-replace-children! node-id children)
    (user-app-patch!
      (list
        (cons 'op "replace-children")
        (cons 'id node-id)
        (cons 'children children))))

  (winscheme-doc 'user-app-append-child! "Imperatively append a new child to a container node.")
  (define (user-app-append-child! node-id child)
    (user-app-patch!
      (list
        (cons 'op "append-child")
        (cons 'id node-id)
        (cons 'child child))))

  (winscheme-doc 'user-app-insert-child! "Imperatively insert a child node at a given index inside a container.")
  (define (user-app-insert-child! node-id index child)
    (user-app-patch!
      (list
        (cons 'op "insert-child")
        (cons 'id node-id)
        (cons 'index index)
        (cons 'child child))))

  (winscheme-doc 'user-app-prepend-child! "Imperatively prepend a child node to a container.")
  (define (user-app-prepend-child! node-id child)
    (user-app-insert-child! node-id 0 child))

  (winscheme-doc 'user-app-prepend-text-child! "Imperatively prepend a text child node with a known id to a container.")
  (define (user-app-prepend-text-child! node-id child-id text . rest)
    (user-app-prepend-child!
      node-id
      (apply user-app-text text 'id child-id rest)))

  (winscheme-doc 'user-app-remove-child! "Imperatively remove a child from a container node.")
  (define (user-app-remove-child! node-id . rest)
    (let ((target (if (null? rest) 0 (car rest))))
      (user-app-patch!
        (if (or (symbol? target) (string? target))
            (list
              (cons 'op "remove-child")
              (cons 'id node-id)
              (cons 'childId (if (symbol? target) (symbol->string target) target)))
            (list
              (cons 'op "remove-child")
              (cons 'id node-id)
              (cons 'index target))))))

  (winscheme-doc 'user-app-move-child! "Imperatively move an existing child node to a new index inside a container.")
  (define (user-app-move-child! node-id child-id index)
    (user-app-patch!
      (list
        (cons 'op "move-child")
        (cons 'id node-id)
        (cons 'childId (if (symbol? child-id) (symbol->string child-id) child-id))
        (cons 'index index))))

  (winscheme-doc 'user-app-run! "Publish a static UI tree and block waiting for window close." 'prim #t)
  (define (user-app-run! spec)
    (and (user-app-show! spec)
         (not (zero? (winscheme-user-app-raw-run-host)))))

  (winscheme-doc 'user-app-open-folder-dialog "Prompt the user with a native Choose Directory dialog.")
  (define (user-app-open-folder-dialog . maybe-title)
    (let ((chosen (winscheme-user-app-raw-choose-folder
                    (if (null? maybe-title) "" (car maybe-title)))))
      (if (or (not chosen) (string=? chosen ""))
          #f
          chosen)))

  (define (winscheme-user-app-dispatch-event-json json-text)
    (winscheme-user-app-load-json!)
    (let ((event (json-read-string json-text)))
      (set! user-app-last-event-value event)
      (when user-app-event-handler
        (dynamic-wind
          (lambda ()
            (set! user-app-event-depth (+ user-app-event-depth 1)))
          (lambda ()
            (user-app-event-handler event))
          (lambda ()
            (set! user-app-event-depth (max 0 (- user-app-event-depth 1)))
            (when (= user-app-event-depth 0)
              (when user-app-rerender-pending?
                (set! user-app-rerender-pending? #f)
                (user-app-run-reconcile!))
              (for-each (lambda (hook) (hook))
                        user-app-post-event-hooks)))))
      event))

  (define (user-app-demo)
    (user-app-window
      "WinScheme Declarative App"
      (user-app-stack
        (user-app-card
          "Welcome"
          (user-app-heading "Scheme can publish a UI tree")
          (user-app-text "This first pass writes a declarative app spec that the separate WebView2 host renders."))
        (user-app-row
          (user-app-button "Reload Spec" 'reload-spec 'appearance "secondary")
          (user-app-button "Ping Host" 'ping-host))
        (user-app-card
          "Input Example"
          (user-app-input "Name" "" 'name-changed 'placeholder "Type here")))))
)
