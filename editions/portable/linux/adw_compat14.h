/* libadwaita 1.4 (AppImage, debian:12) has no AdwDialog / AdwAlertDialog: map the subset used by adw_v062.inc / adw_ux.inc onto AdwMessageDialog / GtkWindow */
#if !ADW_CHECK_VERSION(1, 5, 0)
typedef GtkWidget AdwDialog;
typedef AdwMessageDialog AdwAlertDialog;
#define ADW_ALERT_DIALOG(o) ADW_MESSAGE_DIALOG(o)
static inline GtkWidget *compat_alert_new(const char *h, const char *b){ GtkWidget *d=adw_message_dialog_new(NULL,h,b); gtk_window_set_modal(GTK_WINDOW(d),TRUE); return d; }
#define adw_alert_dialog_new(h,b) compat_alert_new(h,b)
#define adw_alert_dialog_add_responses adw_message_dialog_add_responses
#define adw_alert_dialog_set_close_response adw_message_dialog_set_close_response
#define adw_alert_dialog_set_default_response adw_message_dialog_set_default_response
#define adw_alert_dialog_set_extra_child adw_message_dialog_set_extra_child
#define adw_alert_dialog_set_response_appearance adw_message_dialog_set_response_appearance
static inline void compat_dialog_present(GtkWidget *d, GtkWidget *parent){ gtk_window_set_transient_for(GTK_WINDOW(d),GTK_WINDOW(parent)); gtk_window_present(GTK_WINDOW(d)); }
#define adw_dialog_present(d,p) compat_dialog_present(GTK_WIDGET(d),p)
static inline GtkWidget *compat_dialog_new(void){ GtkWidget *w=gtk_window_new(); gtk_window_set_modal(GTK_WINDOW(w),TRUE); return w; }
#define adw_dialog_new() compat_dialog_new()
#define adw_dialog_close(d) gtk_window_close(GTK_WINDOW(d))
#define adw_dialog_set_title(d,t) gtk_window_set_title(GTK_WINDOW(d),t)
#define adw_dialog_set_child(d,c) gtk_window_set_child(GTK_WINDOW(d),c)
#define adw_dialog_set_focus(d,f) gtk_window_set_focus(GTK_WINDOW(d),f)
#define adw_dialog_set_content_width(d,v) g_object_set_data(G_OBJECT(d),"cw",GINT_TO_POINTER(v))
#define adw_dialog_set_content_height(d,v) gtk_window_set_default_size(GTK_WINDOW(d),GPOINTER_TO_INT(g_object_get_data(G_OBJECT(d),"cw")),v)
#endif
