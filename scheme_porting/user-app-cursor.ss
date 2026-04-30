;;; user-app-cursor.ss
;;;
;;; Database cursor abstraction for WinScheme declarative user apps.
;;;
;;; A cursor is a named record pointer into an ordered row set.  Multiple
;;; views (grid, form fields, navigator, computed text) observe the same
;;; cursor and update automatically via targeted patch operations when the
;;; cursor moves — no full rerender is needed.
;;;
;;; Load after the backend module:
;;;
;;;   (winscheme-load-module "user-app-native.ss")   ; or web backend
;;;   (winscheme-load-module "user-app-cursor.ss")
;;;
;;; See database_cursor.md for the full design specification.

(begin

  ;; --------------------------------------------------------------------------
  ;; Internal utilities
  ;; --------------------------------------------------------------------------

  ;; Look up key in alist, trying both symbol and string forms so that the
  ;; cursor works with rows from both db-mysql (string keys) and any
  ;; symbol-keyed alist the caller constructs directly.
  (define (cursor-alist-ref alist key . rest)
    (let ((default (if (null? rest) "" (car rest))))
      (let ((pair (or (assoc key alist)
                      (if (symbol? key)
                          (assoc (symbol->string key) alist)
                          (assoc (string->symbol key) alist)))))
        (if pair (cdr pair) default))))

  ;; True when two alist keys name the same logical column, treating symbol
  ;; and string forms as equivalent.  'name and "name" collapse to the same
  ;; entry so that editing a MySQL row (string keys) with a symbol column
  ;; name does not leave duplicate entries in the edit buffer.
  (define (cursor-keys-equal? a b)
    (or (equal? a b)
        (and (symbol? a) (string? b) (string=? (symbol->string a) b))
        (and (string? a) (symbol? b) (string=? a (symbol->string b)))))

  ;; Return a new alist with key set to value, removing any prior entry with
  ;; the same logical column name (symbol/string equivalent).
  (define (cursor-alist-set alist key value)
    (cons (cons key value)
          (let loop ((rest alist))
            (cond
              ((null? rest) '())
              ((cursor-keys-equal? (caar rest) key) (loop (cdr rest)))
              (else (cons (car rest) (loop (cdr rest))))))))

  ;; Pull a keyword argument from a plist, returning default if absent.
  (define (cursor-opt opts key default)
    (let loop ((rest opts))
      (cond
        ((or (null? rest) (null? (cdr rest))) default)
        ((eq? (car rest) key) (cadr rest))
        (else (loop (cddr rest))))))

  ;; Convert any column value to a display string.
  (define (cursor-value->string v)
    (cond
      ((string? v)  v)
      ((number? v)  (number->string v))
      ((boolean? v) (if v "true" "false"))
      (else         "")))

  ;; Normalize a row id to a string for comparison and emission.
  ;; DB primary keys are commonly integers; the UI layer always emits string
  ;; ids.  Without this, (equal? 42 "42") is #f and row-click navigation
  ;; silently fails for numeric primary keys.
  ;;
  ;; Handles: string → pass-through, integer/float → decimal string,
  ;; #f / #t / other → "".  Non-numeric non-string ids (e.g. symbols used
  ;; as keys) return "" which means the row will not be selected rather than
  ;; being matched incorrectly.  For all normal DB-backed cursors (integer
  ;; or string primary keys) this covers every realistic case.
  (define (cursor-id->string v)
    (cond
      ((string? v)  v)
      ((number? v)
       (let ((n (inexact->exact v)))
         (if (integer? n)
             (number->string n)
             (number->string v))))
      ((not v) "")
      (else    "")))

  ;; --------------------------------------------------------------------------
  ;; Cursor record — stored as a vector for broad Chez compatibility
  ;;
  ;; Slots:
  ;;  0  id              string   cursor identifier, used to derive node-ids
  ;;  1  columns         list     column symbols  e.g. '(id name email)
  ;;  2  rows            list     in-memory row list (alists with symbol keys)
  ;;  3  current-index   integer  0-based index of the current row
  ;;  4  page-size       integer  rows per page for navigation
  ;;  5  state           symbol   'browse | 'edit | 'insert
  ;;  6  dirty           bool     unsaved edits exist in the edit buffer
  ;;  7  edit-buffer     alist    current row copy being edited
  ;;  8  field-bindings  list     ((node-id column type) ...)
  ;;  9  table-id        string   node-id of the grid view, or #f
  ;; 10  navigator-id    string   node-id of the navigator bar, or #f
  ;; 11  computed-bnd    list     ((node-id . lambda) ...)
  ;; 12  load-page-fn    proc     #f or (lambda (cursor page) -> rows)
  ;; 13  version-column  string   column name holding the optimistic-concurrency
  ;;                              token (e.g. "updated_at"), or #f
  ;; 14  validators      alist    ((column . predicate) ...) run by cursor-set!
  ;; --------------------------------------------------------------------------

  (define cursor-SLOTS 15)
  (define cursor-id-slot           0)
  (define cursor-columns-slot      1)
  (define cursor-rows-slot         2)
  (define cursor-index-slot        3)
  (define cursor-page-size-slot    4)
  (define cursor-state-slot        5)
  (define cursor-dirty-slot        6)
  (define cursor-edit-buf-slot     7)
  (define cursor-field-bnd-slot    8)
  (define cursor-table-id-slot     9)
  (define cursor-nav-id-slot       10)
  (define cursor-computed-bnd-slot 11)
  (define cursor-load-page-slot    12)
  (define cursor-version-col-slot  13)
  (define cursor-validators-slot   14)

  (define (cursor? v)
    (and (vector? v) (= (vector-length v) cursor-SLOTS)))

  (define (cursor-get c slot)   (vector-ref  c slot))
  (define (cursor-set! c slot v) (vector-set! c slot v))

  ;; Named accessors and mutators
  (define (cursor-id            c) (cursor-get c cursor-id-slot))
  (define (cursor-columns       c) (cursor-get c cursor-columns-slot))
  (define (cursor-rows          c) (cursor-get c cursor-rows-slot))
  (define (cursor-current-index c) (cursor-get c cursor-index-slot))
  (define (cursor-page-size     c) (cursor-get c cursor-page-size-slot))
  (define (cursor-state         c) (cursor-get c cursor-state-slot))
  (define (cursor-dirty         c) (cursor-get c cursor-dirty-slot))
  (define (cursor-edit-buffer   c) (cursor-get c cursor-edit-buf-slot))
  (define (cursor-field-bindings c) (cursor-get c cursor-field-bnd-slot))
  (define (cursor-table-id      c) (cursor-get c cursor-table-id-slot))
  (define (cursor-navigator-id  c) (cursor-get c cursor-nav-id-slot))
  (define (cursor-computed-bindings c) (cursor-get c cursor-computed-bnd-slot))
  (define (cursor-load-page-fn    c) (cursor-get c cursor-load-page-slot))
  (define (cursor-version-column  c) (cursor-get c cursor-version-col-slot))
  (define (cursor-validators      c) (cursor-get c cursor-validators-slot))

  (define (set-cursor-rows!             c v) (cursor-set! c cursor-rows-slot        v))
  (define (set-cursor-current-index!    c v) (cursor-set! c cursor-index-slot       v))
  (define (set-cursor-state!            c v) (cursor-set! c cursor-state-slot       v))
  (define (set-cursor-dirty!            c v) (cursor-set! c cursor-dirty-slot       v))
  (define (set-cursor-edit-buffer!      c v) (cursor-set! c cursor-edit-buf-slot    v))
  (define (set-cursor-field-bindings!   c v) (cursor-set! c cursor-field-bnd-slot   v))
  (define (set-cursor-table-id!         c v) (cursor-set! c cursor-table-id-slot    v))
  (define (set-cursor-navigator-id!     c v) (cursor-set! c cursor-nav-id-slot      v))
  (define (set-cursor-computed-bindings! c v)(cursor-set! c cursor-computed-bnd-slot v))

  ;; --------------------------------------------------------------------------
  ;; Patch backend
  ;; --------------------------------------------------------------------------

  ;; Settable patch function.  Defaults to user-app-native-patch! which is
  ;; available after loading user-app-native.ss.  For the web backend, call:
  ;;   (user-app-cursor-set-patch-fn! user-app-patch!)
  (define cursor-patch-fn! #f)

  (winscheme-doc 'user-app-cursor-set-patch-fn! "Install the patch function used to send cursor UI updates, overriding the native backend default.")
  (define (user-app-cursor-set-patch-fn! fn)
    (set! cursor-patch-fn! fn))

  (define (cursor-do-patch! patch)
    (let ((fn (or cursor-patch-fn! user-app-native-patch!)))
      (fn patch)))

  ;; --------------------------------------------------------------------------
  ;; Construction
  ;; --------------------------------------------------------------------------

  (winscheme-doc 'make-user-app-cursor "Create a cursor for declarative user-app views from rows, columns, and optional paging or validation settings.")
  (define (make-user-app-cursor . args)
    (let ((c (make-vector cursor-SLOTS #f)))
      (cursor-set! c cursor-id-slot          (cursor-opt args 'id ""))
      (cursor-set! c cursor-columns-slot     (cursor-opt args 'columns '()))
      (cursor-set! c cursor-rows-slot        (cursor-opt args 'rows '()))
      (cursor-set! c cursor-index-slot       0)
      (cursor-set! c cursor-page-size-slot   (cursor-opt args 'page-size 25))
      (cursor-set! c cursor-state-slot       'browse)
      (cursor-set! c cursor-dirty-slot       #f)
      (cursor-set! c cursor-edit-buf-slot    '())
      (cursor-set! c cursor-field-bnd-slot   '())
      (cursor-set! c cursor-table-id-slot    #f)
      (cursor-set! c cursor-nav-id-slot      #f)
      (cursor-set! c cursor-computed-bnd-slot '())
      (cursor-set! c cursor-load-page-slot   (cursor-opt args 'load-page #f))
      (cursor-set! c cursor-version-col-slot (cursor-opt args 'version-column #f))
      (cursor-set! c cursor-validators-slot  (cursor-opt args 'validators '()))
      c))

  ;; --------------------------------------------------------------------------
  ;; Internal helpers
  ;; --------------------------------------------------------------------------

  (define (cursor-row-count c)
    (length (cursor-rows c)))

  ;; In edit/insert state, return the edit buffer; otherwise the row at index.
  (define (cursor-effective-row c)
    (case (cursor-state c)
      ((edit insert) (cursor-edit-buffer c))
      (else
       (let ((rows (cursor-rows c))
             (idx  (cursor-current-index c)))
         (if (and (>= idx 0) (< idx (length rows)))
             (list-ref rows idx)
             '())))))

  ;; Return 0-based index of row with given id, or -1 if not found.
  ;; Both the stored id and the search value are normalized to strings so
  ;; that numeric DB primary keys and the string ids emitted by the UI
  ;; compare equal correctly.
  (define (cursor-index-by-id c row-id)
    (let ((target (cursor-id->string row-id)))
      (let loop ((rows (cursor-rows c)) (i 0))
        (cond
          ((null? rows) -1)
          ((equal? target
                   (cursor-id->string (cursor-alist-ref (car rows) 'id "")))
           i)
          (else (loop (cdr rows) (+ i 1)))))))

  (define (cursor-clamp c idx)
    (let ((n (cursor-row-count c)))
      (if (= n 0) 0 (max 0 (min idx (- n 1))))))

  (define (cursor-node-id c suffix)
    (string-append (cursor-id c) "-" suffix))

  ;; Return a string representation of a column key — symbol or string both
  ;; accepted so callers can use whichever form matches their DB module.
  (define (cursor-col->string col)
    (if (symbol? col) (symbol->string col) col))

  ;; --------------------------------------------------------------------------
  ;; Patch builders
  ;; --------------------------------------------------------------------------

  (define (cursor-set-node-props-op node-id props)
    (list (cons 'op "set-node-props")
          (cons 'id node-id)
          (cons 'props props)))

  (define (cursor-set-value-op node-id value)
    (cursor-set-node-props-op node-id (list (cons 'value value))))

  (define (cursor-set-checked-op node-id checked)
    (cursor-set-node-props-op node-id (list (cons 'checked checked))))

  (define (cursor-set-text-op node-id text)
    (cursor-set-node-props-op node-id (list (cons 'text text))))

  (define (cursor-set-selected-id-op node-id row-id)
    (cursor-set-node-props-op node-id (list (cons 'selectedId row-id))))

  ;; Position string displayed in the navigator bar.
  (define (cursor-position-text c)
    (let ((n   (cursor-row-count c))
          (idx (cursor-current-index c))
          (st  (cursor-state c)))
      (cond
        ((eq? st 'insert) "New row")
        ((eq? st 'edit)
         (string-append "Editing " (number->string (+ idx 1))
                        " of " (number->string n)))
        ((= n 0) "No rows")
        (else
         (string-append "Row " (number->string (+ idx 1))
                        " of " (number->string n))))))

  ;; Build the complete list of patch ops needed to bring all bound views
  ;; up to date with the cursor's current position and state.
  (define (cursor-collect-patches c)
    (let ((row     (cursor-effective-row c))
          (patches '()))

      ;; Field controls
      (for-each
        (lambda (binding)
          (let* ((node-id (car binding))
                 (column  (cadr binding))
                 (type    (caddr binding))
                 (raw     (cursor-alist-ref row column ""))
                 (op (if (eq? type 'checkbox)
                         (cursor-set-checked-op
                           node-id
                           (cond ((boolean? raw) raw)
                                 ((string? raw)  (string=? raw "true"))
                                 (else           #f)))
                         (cursor-set-value-op node-id (cursor-value->string raw)))))
            (set! patches (cons op patches))))
        (cursor-field-bindings c))

      ;; Computed text views
      (for-each
        (lambda (pair)
          (set! patches
            (cons (cursor-set-text-op (car pair) ((cdr pair) row))
                  patches)))
        (cursor-computed-bindings c))

      ;; Grid selection — id must be a string; the table control and the
      ;; UI event both use strings, so normalize numeric DB ids here.
      (when (cursor-table-id c)
        (set! patches
          (cons (cursor-set-selected-id-op
                  (cursor-table-id c)
                  (cursor-id->string (cursor-alist-ref row 'id "")))
                patches)))

      ;; Navigator position text (via node-id "<nav-id>-position")
      (when (cursor-navigator-id c)
        (set! patches
          (cons (cursor-set-text-op
                  (string-append (cursor-navigator-id c) "-position")
                  (cursor-position-text c))
                patches)))

      (reverse patches)))

  ;; Send all view patches in one batch call.
  (define (cursor-emit-patches! c)
    (let ((ops (cursor-collect-patches c)))
      (unless (null? ops)
        (cursor-do-patch! (list->vector ops)))))

  ;; --------------------------------------------------------------------------
  ;; Data loading
  ;; --------------------------------------------------------------------------

  ;; Load rows without patching (use before the window is shown).
  (winscheme-doc 'user-app-cursor-load! "Load cursor rows and reset browse state without emitting UI patches.")
  (define (user-app-cursor-load! c rows)
    (set-cursor-rows! c rows)
    (set-cursor-current-index! c 0)
    (set-cursor-state! c 'browse)
    (set-cursor-dirty! c #f)
    (set-cursor-edit-buffer! c '()))

  ;; Load rows and patch all bound views.
  (winscheme-doc 'user-app-cursor-set-rows! "Replace cursor rows and patch all bound cursor views.")
  (define (user-app-cursor-set-rows! c rows)
    (user-app-cursor-load! c rows)
    (cursor-emit-patches! c))

  ;; Reload the cursor from a freshly fetched row list and patch all views.
  ;; The caller is responsible for querying the database and extracting rows
  ;; in the correct format (string-keyed alists).  Examples:
  ;;
  ;;   MySQL:
  ;;     (user-app-cursor-reload! c
  ;;       (db-mysql-query-row-alists (db-mysql-query conn sql)))
  ;;
  ;;   SQLite (supports bind parameters natively):
  ;;     (user-app-cursor-reload! c
  ;;       (sqlite-query db sql arg1 arg2))
  ;;
  ;; This keeps the cursor backend-agnostic: it accepts any list of alists
  ;; without knowing which database module produced them.
  (winscheme-doc 'user-app-cursor-reload! "Reload a cursor from freshly fetched rows and patch all bound views.")
  (define (user-app-cursor-reload! c rows)
    (user-app-cursor-set-rows! c rows))

  ;; --------------------------------------------------------------------------
  ;; Reading
  ;; --------------------------------------------------------------------------

  (winscheme-doc 'user-app-cursor-current "Return the cursor's current effective row, using the edit buffer while editing or inserting.")
  (define (user-app-cursor-current c) (cursor-effective-row c))
  (winscheme-doc 'user-app-cursor-ref "Read a column from the cursor's current effective row.")
  (define (user-app-cursor-ref c col) (cursor-alist-ref (cursor-effective-row c) col ""))
  (define (user-app-cursor-index c)   (cursor-current-index c))
  (define (user-app-cursor-count c)   (cursor-row-count c))
  (winscheme-doc 'user-app-cursor-state "Return the cursor state symbol: browse, edit, or insert.")
  (define (user-app-cursor-state c)   (cursor-state c))
  (winscheme-doc 'user-app-cursor-dirty? "Return #t when the cursor has unsaved edits in its edit buffer.")
  (define (user-app-cursor-dirty? c)  (cursor-dirty c))

  ;; Return the current row's value for the version column, or "" if none
  ;; is configured.  Pass this to the UPDATE predicate for optimistic
  ;; concurrency: WHERE id=? AND <version-col>=?
  (winscheme-doc 'user-app-cursor-version "Return the current version-column value for optimistic-concurrency checks, or an empty string when none is configured.")
  (define (user-app-cursor-version c)
    (let ((col (cursor-version-column c)))
      (if col
          (cursor-alist-ref (cursor-effective-row c) col "")
          "")))

  ;; #t when all fields in the edit buffer pass their declared validators.
  ;; Always #t in browse state (no edits pending).
  (winscheme-doc 'user-app-cursor-valid? "Return #t when the current cursor edit buffer passes all configured validators.")
  (define (user-app-cursor-valid? c)
    (if (eq? (cursor-state c) 'browse)
        #t
        (let ((validators (cursor-validators c))
              (buf        (cursor-edit-buffer c)))
          (let loop ((vs validators))
            (or (null? vs)
                (let* ((col  (caar vs))
                       (pred (cdar vs))
                       (val  (cursor-alist-ref buf col "")))
                  (and (pred val) (loop (cdr vs)))))))))

  ;; Return an alist of (column . message) for every validator that fails
  ;; on the current edit buffer.  Empty list means all fields are valid.
  ;; Always returns '() in browse state.
  (winscheme-doc 'user-app-cursor-validation-errors "Return validation errors for the current cursor edit buffer as an alist of column/message pairs.")
  (define (user-app-cursor-validation-errors c)
    (if (eq? (cursor-state c) 'browse)
        '()
        (let ((validators (cursor-validators c))
              (buf        (cursor-edit-buffer c)))
          (let loop ((vs validators) (errors '()))
            (if (null? vs)
                (reverse errors)
                (let* ((col  (caar vs))
                       (pred (cdar vs))
                       (val  (cursor-alist-ref buf col "")))
                  (loop (cdr vs)
                        (if (pred val)
                            errors
                            (cons (cons col
                                        (string-append (cursor-col->string col)
                                                       " is invalid"))
                                  errors)))))))))

  ;; Return the validation error message for a single column, or #f if valid.
  (winscheme-doc 'user-app-cursor-error-for "Return the validation error message for one cursor column, or #f if the field is valid.")
  (define (user-app-cursor-error-for c column)
    (let ((errors (user-app-cursor-validation-errors c)))
      (let ((pair (or (assoc column errors)
                      (if (symbol? column)
                          (assoc (symbol->string column) errors)
                          (assoc (string->symbol column) errors)))))
        (if pair (cdr pair) #f))))

  (define (user-app-cursor-at-first? c)
    (= (cursor-current-index c) 0))

  (define (user-app-cursor-at-last? c)
    (let ((n (cursor-row-count c)))
      (or (= n 0) (= (cursor-current-index c) (- n 1)))))

  ;; --------------------------------------------------------------------------
  ;; Navigation
  ;; --------------------------------------------------------------------------

  (define (cursor-move-to-index! c idx)
    (set-cursor-current-index! c (cursor-clamp c idx))
    (set-cursor-state! c 'browse)
    (set-cursor-edit-buffer! c '())
    (set-cursor-dirty! c #f)
    (cursor-emit-patches! c)
    #t)

  (winscheme-doc 'user-app-cursor-first! "Move the cursor to the first row and patch bound views.")
  (define (user-app-cursor-first! c)
    (if (= (cursor-row-count c) 0) #f (cursor-move-to-index! c 0)))

  (winscheme-doc 'user-app-cursor-last! "Move the cursor to the last row and patch bound views.")
  (define (user-app-cursor-last! c)
    (let ((n (cursor-row-count c)))
      (if (= n 0) #f (cursor-move-to-index! c (- n 1)))))

  (winscheme-doc 'user-app-cursor-next! "Move the cursor forward by one row and patch bound views.")
  (define (user-app-cursor-next! c)
    (let ((idx (cursor-current-index c)) (n (cursor-row-count c)))
      (if (>= idx (- n 1)) #f (cursor-move-to-index! c (+ idx 1)))))

  (winscheme-doc 'user-app-cursor-prev! "Move the cursor backward by one row and patch bound views.")
  (define (user-app-cursor-prev! c)
    (let ((idx (cursor-current-index c)))
      (if (<= idx 0) #f (cursor-move-to-index! c (- idx 1)))))

  (winscheme-doc 'user-app-cursor-next-page! "Advance the cursor by one page of rows and patch bound views.")
  (define (user-app-cursor-next-page! c)
    (let ((idx (cursor-current-index c)) (n (cursor-row-count c))
          (pg  (cursor-page-size c)))
      (if (>= idx (- n 1)) #f
          (cursor-move-to-index! c (min (+ idx pg) (- n 1))))))

  (winscheme-doc 'user-app-cursor-prev-page! "Move the cursor backward by one page of rows and patch bound views.")
  (define (user-app-cursor-prev-page! c)
    (let ((idx (cursor-current-index c)) (pg (cursor-page-size c)))
      (if (<= idx 0) #f
          (cursor-move-to-index! c (max (- idx pg) 0)))))

  (winscheme-doc 'user-app-cursor-move-to! "Move the cursor to the row whose id matches the supplied row id.")
  (define (user-app-cursor-move-to! c row-id)
    (let ((idx (cursor-index-by-id c row-id)))
      (if (= idx -1) #f (cursor-move-to-index! c idx))))

  ;; --------------------------------------------------------------------------
  ;; Editing
  ;; --------------------------------------------------------------------------

  (winscheme-doc 'user-app-cursor-begin-edit! "Enter edit mode using the current row as the cursor edit buffer.")
  (define (user-app-cursor-begin-edit! c)
    (when (eq? (cursor-state c) 'browse)
      (set-cursor-edit-buffer! c (cursor-effective-row c))
      (set-cursor-state! c 'edit)
      (cursor-emit-patches! c)))

  (winscheme-doc 'user-app-cursor-begin-insert! "Enter insert mode with a blank edit buffer built from the cursor columns.")
  (define (user-app-cursor-begin-insert! c)
    (when (eq? (cursor-state c) 'browse)
      (set-cursor-edit-buffer! c
        (map (lambda (col) (cons col "")) (cursor-columns c)))
      (set-cursor-state! c 'insert)
      (cursor-emit-patches! c)))

  ;; Update a field in the edit buffer.  Moves to edit state automatically.
  ;; Runs the column's validator (if any) before mutating; returns #f without
  ;; changing state when validation fails.  Returns #t on success.
  (winscheme-doc 'user-app-cursor-set! "Update a field in the cursor edit buffer, automatically entering edit mode and enforcing any configured validator.")
  (define (user-app-cursor-set! c column value)
    (let* ((validators (cursor-validators c))
           (pred       (cursor-alist-ref validators column #f)))
      (if (and pred (not (pred value)))
          #f   ; validation failed — leave state unchanged
          (begin
            (when (eq? (cursor-state c) 'browse)
              (set-cursor-edit-buffer! c (cursor-effective-row c))
              (set-cursor-state! c 'edit))
            (set-cursor-edit-buffer! c
              (cursor-alist-set (cursor-edit-buffer c) column value))
            (set-cursor-dirty! c #t)
            #t))))

  ;; Discard edits and return to browse.
  (winscheme-doc 'user-app-cursor-cancel! "Discard pending cursor edits and return to browse mode.")
  (define (user-app-cursor-cancel! c)
    (unless (eq? (cursor-state c) 'browse)
      (set-cursor-state! c 'browse)
      (set-cursor-edit-buffer! c '())
      (set-cursor-dirty! c #f)
      (cursor-emit-patches! c)))

  ;; Commit the edit buffer.  In edit state, writes back into the row list.
  ;; In insert state, the caller must reload rows from the DB.
  ;; Call AFTER executing the database write.
  (winscheme-doc 'user-app-cursor-post! "Commit the cursor edit buffer to in-memory rows after a successful database write and return to browse mode.")
  (define (user-app-cursor-post! c)
    (when (eq? (cursor-state c) 'edit)
      (let ((idx (cursor-current-index c))
            (buf (cursor-edit-buffer c)))
        (let loop ((rows (cursor-rows c)) (i 0) (acc '()))
          (if (null? rows)
              (set-cursor-rows! c (reverse acc))
              (loop (cdr rows) (+ i 1)
                    (cons (if (= i idx) buf (car rows)) acc))))))
    (set-cursor-state! c 'browse)
    (set-cursor-edit-buffer! c '())
    (set-cursor-dirty! c #f)
    (cursor-emit-patches! c))

  ;; Remove the current row from the in-memory list.
  ;; Call AFTER executing the database DELETE.
  (winscheme-doc 'user-app-cursor-delete-current! "Remove the current row from the cursor after a successful database delete and patch bound views.")
  (define (user-app-cursor-delete-current! c)
    (let* ((idx      (cursor-current-index c))
           (new-rows (let loop ((rows (cursor-rows c)) (i 0))
                       (if (null? rows) '()
                           (if (= i idx)
                               (loop (cdr rows) (+ i 1))
                               (cons (car rows) (loop (cdr rows) (+ i 1))))))))
      (set-cursor-rows! c new-rows)
      (set-cursor-state! c 'browse)
      (set-cursor-dirty! c #f)
      (set-cursor-current-index! c
        (let ((n (length new-rows)))
          (if (= n 0) 0 (min idx (- n 1)))))
      (cursor-emit-patches! c)))

  ;; --------------------------------------------------------------------------
  ;; Binding registry — called by view helpers at render time
  ;; --------------------------------------------------------------------------

  (define (cursor-register-field! c node-id column type)
    (set-cursor-field-bindings! c
      (cons (list node-id column type)
            (filter (lambda (b) (not (equal? (car b) node-id)))
                    (cursor-field-bindings c)))))

  (define (cursor-register-computed! c node-id fn)
    (set-cursor-computed-bindings! c
      (cons (cons node-id fn)
            (filter (lambda (p) (not (equal? (car p) node-id)))
                    (cursor-computed-bindings c)))))

  ;; --------------------------------------------------------------------------
  ;; View helpers — return declarative UI nodes
  ;; --------------------------------------------------------------------------

  ;; Grid view: full table with cursor row highlighted.
  ;; opts: 'id 'label 'select-event 'columns
  (winscheme-doc 'user-app-cursor-table "Create a table node bound to the cursor's rows and current selection.")
  (define (user-app-cursor-table c . opts)
    (let* ((node-id (cursor-opt opts 'id (cursor-node-id c "grid")))
           (label   (cursor-opt opts 'label ""))
           (ev      (cursor-opt opts 'select-event 'cursor-row-selected))
           (columns (cursor-opt opts 'columns '()))
           (rows    (cursor-rows c))
           (cur-id  (cursor-id->string (cursor-alist-ref (cursor-effective-row c) 'id ""))))
      (set-cursor-table-id! c node-id)
      (user-app-table label columns rows ev 'id node-id 'selectedId cur-id)))

  ;; Text input bound to a cursor column.
  ;; opts: 'id 'placeholder and any other user-app-input keywords
  (winscheme-doc 'user-app-cursor-input "Create a text input node bound to a cursor column.")
  (define (user-app-cursor-input c column label event . opts)
    (let* ((node-id (cursor-opt opts 'id (cursor-node-id c (cursor-col->string column))))
           (value   (cursor-value->string
                      (cursor-alist-ref (cursor-effective-row c) column "")))
           (rest    (filter (lambda (x) (not (eq? x 'id))) opts)))
      (cursor-register-field! c node-id column 'input)
      (apply user-app-input label value event 'id node-id rest)))

  ;; Multiline textarea bound to a cursor column.
  (winscheme-doc 'user-app-cursor-textarea "Create a textarea node bound to a cursor column.")
  (define (user-app-cursor-textarea c column label event . opts)
    (let* ((node-id (cursor-opt opts 'id (cursor-node-id c (cursor-col->string column))))
           (value   (cursor-value->string
                      (cursor-alist-ref (cursor-effective-row c) column "")))
           (rest    (filter (lambda (x) (not (eq? x 'id))) opts)))
      (cursor-register-field! c node-id column 'textarea)
      (apply user-app-textarea label value event 'id node-id rest)))

  ;; Dropdown select bound to a cursor column.
  (winscheme-doc 'user-app-cursor-select "Create a select node bound to a cursor column.")
  (define (user-app-cursor-select c column label event options . opts)
    (let* ((node-id (cursor-opt opts 'id (cursor-node-id c (cursor-col->string column))))
           (value   (cursor-value->string
                      (cursor-alist-ref (cursor-effective-row c) column "")))
           (rest    (filter (lambda (x) (not (eq? x 'id))) opts)))
      (cursor-register-field! c node-id column 'select)
      (apply user-app-select label value event options 'id node-id rest)))

  ;; Checkbox bound to a cursor column.
  (winscheme-doc 'user-app-cursor-checkbox "Create a checkbox node bound to a cursor column.")
  (define (user-app-cursor-checkbox c column label event . opts)
    (let* ((node-id (cursor-opt opts 'id (cursor-node-id c (cursor-col->string column))))
           (raw     (cursor-alist-ref (cursor-effective-row c) column #f))
           (checked (cond ((boolean? raw) raw)
                          ((string? raw)  (string=? raw "true"))
                          (else           #f)))
           (rest    (filter (lambda (x) (not (eq? x 'id))) opts)))
      (cursor-register-field! c node-id column 'checkbox)
      (apply user-app-checkbox label checked event 'id node-id rest)))

  ;; Computed text view.  fn receives the current row alist, returns a string.
  ;; Registered for automatic text patches on navigation.
  (winscheme-doc 'user-app-cursor-text "Create a computed text node that recomputes from the current cursor row on navigation or edits.")
  (define (user-app-cursor-text c fn . opts)
    (let* ((node-id (cursor-opt opts 'id (cursor-node-id c "text")))
           (text    (fn (cursor-effective-row c)))
           (rest    (filter (lambda (x) (not (eq? x 'id))) opts)))
      (cursor-register-computed! c node-id fn)
      (apply user-app-text text 'id node-id rest)))

  ;; --------------------------------------------------------------------------
  ;; Navigator control
  ;; --------------------------------------------------------------------------

  ;; Render a toolbar of navigation and CRUD buttons with a position
  ;; indicator.  Each button is included only if its event symbol appears in
  ;; the 'events list.  The position text node has id "<nav-id>-position" and
  ;; is patched directly on every cursor movement.
  ;;
  ;; Note: button enabled/disabled state based on cursor boundaries and edit
  ;; state is a planned enhancement (requires C++ enabled-prop support).
  ;; Navigation calls return #f at boundaries so functional behaviour is
  ;; always correct even without visual disabling.
  ;;
  ;; opts: 'id 'events
  (winscheme-doc 'user-app-cursor-navigator "Create a navigator toolbar for cursor movement and common edit actions.")
  (define (user-app-cursor-navigator c . opts)
    (let* ((nav-id  (cursor-opt opts 'id (cursor-node-id c "nav")))
           (events  (cursor-opt opts 'events '()))
           (pos-id  (string-append nav-id "-position"))
           (pos     (cursor-position-text c)))
      (set-cursor-navigator-id! c nav-id)
      ;; Build the children list; (and cond expr) returns #f when cond is
      ;; false, and filter pair? removes #f entries cleanly.
      (let ((children
             (filter pair?
               (list
                 (and (memq 'nav-first     events) (user-app-button "|◀" 'nav-first))
                 (and (memq 'nav-prev-page events) (user-app-button "◀◀" 'nav-prev-page))
                 (and (memq 'nav-prev      events) (user-app-button "◀"  'nav-prev))
                 (user-app-text pos 'id pos-id)
                 (and (memq 'nav-next      events) (user-app-button "▶"  'nav-next))
                 (and (memq 'nav-next-page events) (user-app-button "▶▶" 'nav-next-page))
                 (and (memq 'nav-last      events) (user-app-button "▶|" 'nav-last))
                 (and (memq 'nav-edit      events) (user-app-button "✎"  'nav-edit))
                 (and (memq 'nav-insert    events) (user-app-button "+"  'nav-insert))
                 (and (memq 'nav-save      events) (user-app-button "✓"  'nav-save))
                 (and (memq 'nav-cancel    events) (user-app-button "✕"  'nav-cancel))
                 (and (memq 'nav-delete    events) (user-app-button "🗑" 'nav-delete))))))
        ;; Build a row with an explicit id using user-app-node directly so
        ;; the navigator bar itself can be targeted by future patches.
        (user-app-node "row" (list (cons 'id nav-id)) children))))

  ;; --------------------------------------------------------------------------
  ;; Convenience helpers
  ;; --------------------------------------------------------------------------

  ;; Convert a DB row with string keys to cursor format (symbol keys).
  ;; Pass-through if keys are already symbols.
  (winscheme-doc 'user-app-cursor-normalize-row "Normalize one database row to cursor format with symbol column keys.")
  (define (user-app-cursor-normalize-row row)
    (if (or (null? row) (and (pair? (car row)) (symbol? (caar row))))
        row
        (map (lambda (pair)
               (cons (string->symbol
                       (if (string? (car pair))
                           (car pair)
                           (number->string (car pair))))
                     (cdr pair)))
             row)))

  (winscheme-doc 'user-app-cursor-normalize-rows "Normalize a list of database rows to cursor format with symbol column keys.")
  (define (user-app-cursor-normalize-rows rows)
    (map user-app-cursor-normalize-row rows))

  ;; Dispatch all standard nav-* events in one call from your event handler.
  ;; Returns #t if the event was a navigator event, #f otherwise.
  (winscheme-doc 'user-app-cursor-handle-nav! "Handle standard nav-* event names for a cursor and return #t when the event was consumed.")
  (define (user-app-cursor-handle-nav! c event-name)
    (cond
      ((string=? event-name "nav-first")     (user-app-cursor-first!      c) #t)
      ((string=? event-name "nav-prev")      (user-app-cursor-prev!       c) #t)
      ((string=? event-name "nav-next")      (user-app-cursor-next!       c) #t)
      ((string=? event-name "nav-last")      (user-app-cursor-last!       c) #t)
      ((string=? event-name "nav-prev-page") (user-app-cursor-prev-page!  c) #t)
      ((string=? event-name "nav-next-page") (user-app-cursor-next-page!  c) #t)
      ((string=? event-name "nav-edit")      (user-app-cursor-begin-edit!   c) #t)
      ((string=? event-name "nav-insert")    (user-app-cursor-begin-insert! c) #t)
      ((string=? event-name "nav-cancel")    (user-app-cursor-cancel!       c) #t)
      (else #f)))

  ;; --------------------------------------------------------------------------
  ;; cursor-selection — multi-row selection set
  ;;
  ;; Tracks a set of selected row ids independently of the cursor's single
  ;; active-edit pointer.  Used for batch operations (delete selected, export
  ;; selected, bulk status change).
  ;;
  ;; Stored as a 2-slot vector:
  ;;  0  cursor   — the linked cursor (for row validation and grid patching)
  ;;  1  ids      — list of selected row-id strings
  ;; --------------------------------------------------------------------------

  (define cursor-sel-SLOTS     2)
  (define cursor-sel-cur-slot  0)
  (define cursor-sel-ids-slot  1)

  (define (cursor-selection? v)
    (and (vector? v) (= (vector-length v) cursor-sel-SLOTS)))

  ;; Create a selection linked to a cursor.
  (winscheme-doc 'make-cursor-selection "Create a multi-row selection set linked to a cursor.")
  (define (make-cursor-selection c)
    (let ((sel (make-vector cursor-sel-SLOTS #f)))
      (vector-set! sel cursor-sel-cur-slot c)
      (vector-set! sel cursor-sel-ids-slot '())
      sel))

  (define (cursor-sel-cursor sel) (vector-ref sel cursor-sel-cur-slot))
  (define (cursor-sel-ids    sel) (vector-ref sel cursor-sel-ids-slot))
  (define (cursor-sel-set-ids! sel ids) (vector-set! sel cursor-sel-ids-slot ids))

  ;; Add a row to the selection.  No-op if already selected.
  (winscheme-doc 'cursor-selection-add! "Add a row id to a cursor selection set.")
  (define (cursor-selection-add! sel row-id)
    (let ((id (cursor-id->string row-id)))
      (unless (member id (cursor-sel-ids sel))
        (cursor-sel-set-ids! sel (cons id (cursor-sel-ids sel))))))

  ;; Remove a row from the selection.  No-op if not selected.
  (winscheme-doc 'cursor-selection-remove! "Remove a row id from a cursor selection set.")
  (define (cursor-selection-remove! sel row-id)
    (let ((id (cursor-id->string row-id)))
      (cursor-sel-set-ids! sel
        (filter (lambda (x) (not (equal? x id))) (cursor-sel-ids sel)))))

  ;; Toggle selection state of a row.
  (winscheme-doc 'cursor-selection-toggle! "Toggle whether a row id is present in a cursor selection set.")
  (define (cursor-selection-toggle! sel row-id)
    (if (cursor-selection-contains? sel row-id)
        (cursor-selection-remove! sel row-id)
        (cursor-selection-add!    sel row-id)))

  ;; Clear all selections.
  (winscheme-doc 'cursor-selection-clear! "Clear all row ids from a cursor selection set.")
  (define (cursor-selection-clear! sel)
    (cursor-sel-set-ids! sel '()))

  ;; Select all rows in the linked cursor.
  (winscheme-doc 'cursor-selection-all! "Select every row currently loaded in the linked cursor.")
  (define (cursor-selection-all! sel)
    (cursor-sel-set-ids! sel
      (map (lambda (row)
             (cursor-id->string (cursor-alist-ref row 'id "")))
           (cursor-rows (cursor-sel-cursor sel)))))

  ;; #t if the given row id is in the selection.
  (winscheme-doc 'cursor-selection-contains? "Return #t when a row id is currently selected.")
  (define (cursor-selection-contains? sel row-id)
    (if (member (cursor-id->string row-id) (cursor-sel-ids sel)) #t #f))

  ;; Number of selected rows.
  (winscheme-doc 'cursor-selection-count "Return the number of selected row ids in a cursor selection set.")
  (define (cursor-selection-count sel)
    (length (cursor-sel-ids sel)))

  ;; List of selected row-id strings.
  (winscheme-doc 'cursor-selection-ids "Return the selected row ids from a cursor selection set.")
  (define (cursor-selection-ids sel)
    (cursor-sel-ids sel))

  ;; List of full row alists for selected rows (from the linked cursor).
  (winscheme-doc 'cursor-selection-rows "Return the selected full row alists from the linked cursor.")
  (define (cursor-selection-rows sel)
    (let ((ids (cursor-sel-ids sel)))
      (filter (lambda (row)
                (member (cursor-id->string (cursor-alist-ref row 'id "")) ids))
              (cursor-rows (cursor-sel-cursor sel)))))

)
