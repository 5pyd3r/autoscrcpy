;;; lib/init.ss -- AutoScrcpy Scheme runtime library
;;;
;;; Provides user-friendly Scheme API wrapping the C FFI functions
;;; registered via Sforeign_symbol in bindings.c.

;;; ================================================================
;;; C FFI wrappers (foreign-procedure bindings)
;;; These create Scheme-callable procedures from the C functions
;;; registered via Sforeign_symbol in bindings.c.
;;; ================================================================

(define c-inject-keycode
  (foreign-procedure "c-inject-keycode" (int int) void))
(define c-inject-text
  (foreign-procedure "c-inject-text" (string) void))
(define c-inject-touch
  (foreign-procedure "c-inject-touch" (int int int) void))
(define c-inject-scroll
  (foreign-procedure "c-inject-scroll" (int int int int) void))
(define c-set-clipboard
  (foreign-procedure "c-set-clipboard" (string) void))
(define c-expand-notification
  (foreign-procedure "c-expand-notification" () void))
(define c-collapse-panels
  (foreign-procedure "c-collapse-panels" () void))
(define c-set-display-power
  (foreign-procedure "c-set-display-power" (int) void))
(define c-rotate-device
  (foreign-procedure "c-rotate-device" () void))
(define c-start-app
  (foreign-procedure "c-start-app" (string) void))
(define c-sleep-ms
  (foreign-procedure "c-sleep-ms" (int) void))
(define c-log-message
  (foreign-procedure "c-log-message" (int string) void))

;; Synchronous query FFI
(define c-query-device-info
  (foreign-procedure "c-query-device-info" () ptr))
(define c-query-video-size
  (foreign-procedure "c-query-video-size" () ptr))
(define c-query-window-size
  (foreign-procedure "c-query-window-size" () ptr))
(define c-query-clipboard
  (foreign-procedure "c-query-clipboard" () ptr))
(define c-query-frame-capture
  (foreign-procedure "c-query-frame-capture" () ptr))

;;; ================================================================
;;; Log level constants (must match platform/log.h)
;;; ================================================================

(define LOG_LEVEL_VERBOSE 0)
(define LOG_LEVEL_DEBUG   1)
(define LOG_LEVEL_INFO    2)
(define LOG_LEVEL_WARN    3)
(define LOG_LEVEL_ERROR   4)

;;; ================================================================
;;; Keycode symbol -> Android keycode mapping
;;; ================================================================

(define *keycode-map*
  '((home         .   3)
    (back         .   4)
    (power        .  26)
    (menu         .  82)
    (volume-up    .  24)
    (volume-down  .  25)
    (enter        .  66)
    (tab          .  61)
    (space        .  62)
    (dpad-center  .  23)
    (dpad-up      .  19)
    (dpad-down    .  20)
    (dpad-left    .  21)
    (dpad-right   .  22)
    ;; Letters a-z -> Android keycodes 29-54
    (a . 29) (b . 30) (c . 31) (d . 32) (e . 33)
    (f . 34) (g . 35) (h . 36) (i . 37) (j . 38)
    (k . 39) (l . 40) (m . 41) (n . 42) (o . 43)
    (p . 44) (q . 45) (r . 46) (s . 47) (t . 48)
    (u . 49) (v . 50) (w . 51) (x . 52) (y . 53)
    (z . 54)
    ;; Digits 0-9 -> Android keycodes 7-16
    (0 .  7) (1 .  8) (2 .  9) (3 . 10) (4 . 11)
    (5 . 12) (6 . 13) (7 . 14) (8 . 15) (9 . 16)
    ;; Function keys F1-F12 -> Android keycodes 131-142
    (f1  . 131) (f2  . 132) (f3  . 133) (f4  . 134)
    (f5  . 135) (f6  . 136) (f7  . 137) (f8  . 138)
    (f9  . 139) (f10 . 140) (f11 . 141) (f12 . 142)))

(define (keycode-ref sym)
  (let ((entry (assq sym *keycode-map*)))
    (if entry (cdr entry) #f)))

;;; ================================================================
;;; Event callback registry
;;; ================================================================

(define *on-key-callback*        #f)
(define *on-mouse-callback*      #f)
(define *on-frame-callback*      #f)
(define *on-connect-callback*    #f)
(define *on-disconnect-callback* #f)

;;; ================================================================
;;; Touch action constants (must match Android MotionEvent)
;;; ================================================================

(define ACTION_DOWN  0)
(define ACTION_UP    1)
(define ACTION_MOVE  2)

(define (resolve-touch-action action)
  (cond
    ((eq? action 'down)  ACTION_DOWN)
    ((eq? action 'up)    ACTION_UP)
    ((eq? action 'move)  ACTION_MOVE)
    ((integer? action)   action)
    (else
     (log-error (format "inject-touch: unknown action ~a, using DOWN" action))
     ACTION_DOWN)))

;;; ================================================================
;;; Helper: resolve a keycode from symbol or integer
;;; ================================================================

(define (resolve-keycode keycode)
  (cond
    ((integer? keycode) keycode)
    ((symbol? keycode)
     (let ((code (keycode-ref keycode)))
       (unless code
         (log-error (format "inject-keycode: unknown key symbol ~a" keycode)))
       (or code 0)))
    (else
     (log-error (format "inject-keycode: invalid keycode ~a" keycode))
     0)))

;;; ================================================================
;;; Control commands -- each wraps the corresponding c_* FFI function
;;; ================================================================

(define (inject-keycode keycode down?)
  ;; keycode: symbol (looked up in *keycode-map*) or integer
  ;; down?: #t for key down, #f for key up
  (c-inject-keycode (resolve-keycode keycode) (if down? 1 0)))

(define (inject-text text)
  ;; text: string to inject into the device
  (c-inject-text text))

(define (inject-touch x y action)
  ;; x, y: screen coordinates
  ;; action: 'down, 'up, 'move, or integer constant
  (c-inject-touch x y (resolve-touch-action action)))

(define (inject-scroll x y dx dy)
  ;; x, y: screen coordinates
  ;; dx, dy: scroll deltas
  (c-inject-scroll x y dx dy))

(define (set-clipboard text)
  ;; Copy text to device clipboard
  (c-set-clipboard text))

(define (expand-notification)
  ;; Expand the notification panel
  (c-expand-notification))

(define (expand-settings)
  ;; Alias for expand-notification (keeps API symmetric)
  (c-expand-notification))

(define (collapse-panels)
  ;; Collapse notification and settings panels
  (c-collapse-panels))

(define (set-display-power on?)
  ;; on?: #t to turn display on, #f to turn off
  (c-set-display-power (if on? 1 0)))

(define (rotate-device)
  ;; Rotate the device display
  (c-rotate-device))

(define (start-app package)
  ;; package: Android package name string (e.g., "com.android.settings")
  (c-start-app package))

;;; ================================================================
;;; Device state queries (synchronous, blocks until response)
;;; ================================================================

(define (device-info)
  ;; Returns '(width height name connected) or #f on timeout
  (c-query-device-info))

(define (device-width)
  ;; Returns device screen width or #f
  (let ((info (device-info)))
    (if (pair? info) (car info) #f)))

(define (device-height)
  ;; Returns device screen height or #f
  (let ((info (device-info)))
    (if (and (pair? info) (pair? (cdr info)))
        (cadr info) #f)))

(define (device-name)
  ;; Returns device name string or #f
  (let ((info (device-info)))
    (if (and (pair? info) (pair? (cdr info)) (pair? (cddr info)))
        (caddr info) #f)))

(define (is-connected?)
  ;; Returns #t if device is connected, #f otherwise
  (let ((info (device-info)))
    (if (and (pair? info) (pair? (cdr info)) (pair? (cddr info)) (pair? (cdddr info)))
        (not (zero? (cadddr info))) #f)))

(define (video-size)
  ;; Returns '(width . height) of video stream or #f
  (c-query-video-size))

(define (window-size)
  ;; Returns '(width . height) of window or #f
  (c-query-window-size))

(define (get-clipboard)
  ;; Returns clipboard text string or #f
  (c-query-clipboard))

(define (capture-frame)
  ;; Returns NV12 frame data as bytevector or #f
  (c-query-frame-capture))

;;; ================================================================
;;; Event callback registration
;;; ================================================================

(define (on-key callback)
  ;; callback: (lambda (vk down?) ...) where vk is integer, down? is boolean
  (set! *on-key-callback* callback))

(define (on-mouse callback)
  ;; callback: (lambda (x y buttons action) ...)
  (set! *on-mouse-callback* callback))

(define (on-frame callback)
  ;; callback: (lambda (width height) ...)
  (set! *on-frame-callback* callback))

(define (on-connect callback)
  ;; callback: (lambda () ...)
  (set! *on-connect-callback* callback))

(define (on-disconnect callback)
  ;; callback: (lambda () ...)
  (set! *on-disconnect-callback* callback))

;;; ================================================================
;;; Internal: event dispatch -- called from C event_dispatch.c
;;; ================================================================

(define (dispatch-key-event vk down)
  (when *on-key-callback*
    (*on-key-callback* vk (not (zero? down)))))

(define (dispatch-mouse-event x y buttons action)
  (when *on-mouse-callback*
    (*on-mouse-callback* x y buttons action)))

(define (dispatch-frame-event width height)
  (when *on-frame-callback*
    (*on-frame-callback* width height)))

(define (dispatch-connect-event)
  (when *on-connect-callback*
    (*on-connect-callback*)))

(define (dispatch-disconnect-event)
  (when *on-disconnect-callback*
    (*on-disconnect-callback*)))

;;; ================================================================
;;; Utility helpers
;;; ================================================================

(define (sleep-ms ms)
  ;; Sleep for the given number of milliseconds
  (c-sleep-ms ms))

(define (log-info msg)
  ;; Log at INFO level
  (c-log-message LOG_LEVEL_INFO msg))

(define (log-error msg)
  ;; Log at ERROR level
  (c-log-message LOG_LEVEL_ERROR msg))

(define (log-debug msg)
  ;; Log at DEBUG level
  (c-log-message LOG_LEVEL_DEBUG msg))

(define (log-warn msg)
  ;; Log at WARN level
  (c-log-message LOG_LEVEL_WARN msg))

(define (log-verbose msg)
  ;; Log at VERBOSE level
  (c-log-message LOG_LEVEL_VERBOSE msg))

;;; ================================================================
;;; Script loading
;;; ================================================================

(define (load-script path)
  ;; Load and execute a Scheme script file.
  ;; The file is read from disk and evaluated in the current environment.
  (unless (file-exists? path)
    (log-error (format "load-script: file not found: ~a" path))
    (error 'load-script "file not found" path))
  (log-info (format "Loading script: ~a" path))
  (load path))

;;; ================================================================
;;; REPL helpers
;;; ================================================================

(define (open-repl)
  ;; Display information about entering the interactive REPL.
  ;; The actual REPL loop is managed by the engine thread; this just
  ;; prints a helpful message for users invoking it from a script.
  (display "AutoScrcpy Scheme REPL\n")
  (display "Type expressions to evaluate. Ctrl-D or (exit) to quit.\n"))

;;; ================================================================
;;; Key combo helpers -- convenience wrappers for common operations
;;; ================================================================

(define (press-key keycode)
  ;; Simulate a full key press (down + up)
  (inject-keycode keycode #t)
  (sleep-ms 50)
  (inject-keycode keycode #f))

(define (tap x y)
  ;; Simulate a screen tap at (x, y)
  (inject-touch x y 'down)
  (sleep-ms 50)
  (inject-touch x y 'up))

(define (long-press x y duration-ms)
  ;; Simulate a long press at (x, y) for duration-ms milliseconds
  (inject-touch x y 'down)
  (sleep-ms duration-ms)
  (inject-touch x y 'up))

(define (swipe x1 y1 x2 y2 duration-ms steps)
  ;; Swipe from (x1,y1) to (x2,y2) over duration-ms in the given number of steps
  (inject-touch x1 y1 'down)
  (let ((dt (max 1 (quotient duration-ms steps)))
        (dx (/ (- x2 x1) steps))
        (dy (/ (- y2 y1) steps)))
    (let loop ((i 1))
      (when (<= i steps)
        (inject-touch (inexact->exact (round (+ x1 (* dx i))))
                      (inexact->exact (round (+ y1 (* dy i))))
                      'move)
        (sleep-ms dt)
        (loop (+ i 1)))))
  (inject-touch x2 y2 'up))

;;; ================================================================
;;; Startup
;;; ================================================================

(display "AutoScrcpy Scheme runtime loaded.\n")
