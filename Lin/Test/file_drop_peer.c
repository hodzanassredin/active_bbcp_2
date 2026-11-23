/* Native GTK2/X11 drag source for the BlackBox file-drop integration test. */
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>

static gchar *payload, *status_path;
static gsize payload_size;
static const gchar *mime;
static Display *display;
static GdkDragContext *drag;
static gint tx,ty,step,requested,deleted;
static gboolean failed,finished;

static void begin(GtkWidget *widget,GdkDragContext *context,gpointer unused){
	(void)widget;(void)unused;drag=context;
}

static void provide(GtkWidget *widget,GdkDragContext *context,GtkSelectionData *data,guint info,guint time,gpointer unused){
	(void)widget;(void)context;(void)info;(void)time;(void)unused;requested++;
	gtk_selection_data_set(data,gdk_atom_intern(mime,FALSE),8,(const guchar*)payload,(gint)payload_size);
}

static gboolean failure(GtkWidget *widget,GdkDragContext *context,GtkDragResult result,gpointer unused){
	(void)widget;(void)context;(void)result;(void)unused;failed=TRUE;return FALSE;
}

static void remove_source(GtkWidget *widget,GdkDragContext *context,gpointer unused){
	(void)widget;(void)context;(void)unused;deleted++;
}

static void end(GtkWidget *widget,GdkDragContext *context,gpointer unused){
	(void)widget;(void)unused;finished=TRUE;
	gboolean accepted=!failed&&requested>0&&context->action==GDK_ACTION_COPY;
	fprintf(stderr,"native drag end failed=%d requested=%d action=%d\n",failed,requested,context->action);
	const gchar *result=deleted?"deleted\n":accepted?"1\n":"0\n";
	g_file_set_contents(status_path,result,-1,NULL);gtk_main_quit();
}

static gboolean drive(gpointer unused){
	(void)unused;
	if(step==0){fprintf(stderr,"native drag destination=%d,%d\n",tx,ty);XTestFakeMotionEvent(display,-1,45,45,0);XTestFakeButtonEvent(display,1,True,0);}
	else if(step==3)XTestFakeMotionEvent(display,-1,75,50,0);
	else if(step>=5&&step<20)XTestFakeMotionEvent(display,-1,tx,ty,0);
	else if(step==20){fprintf(stderr,"native drag started=%d action=%d\n",drag!=NULL,drag?drag->action:0);XTestFakeButtonEvent(display,1,False,0);}
	else if(step>200){g_file_set_contents(status_path,"timeout\n",-1,NULL);gtk_main_quit();return FALSE;}
	XFlush(display);step++;return !finished;
}

int main(int argc,char **argv){
	gtk_init(&argc,&argv);
	if(argc!=6){fprintf(stderr,"usage: peer payload status x y mime\n");return 2;}
	if(!g_file_get_contents(argv[1],&payload,&payload_size,NULL))return 3;
	status_path=argv[2];tx=atoi(argv[3]);ty=atoi(argv[4]);mime=argv[5];
	GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL),*area=gtk_event_box_new();
	gtk_window_set_title(GTK_WINDOW(window),"BlackBox file-drop native peer");
	gtk_window_set_decorated(GTK_WINDOW(window),FALSE);gtk_window_move(GTK_WINDOW(window),20,20);
	gtk_widget_set_size_request(area,90,60);gtk_container_add(GTK_CONTAINER(window),area);
	GtkTargetEntry target={(gchar*)mime,0,0};
	gtk_drag_source_set(area,GDK_BUTTON1_MASK,&target,1,GDK_ACTION_COPY|GDK_ACTION_MOVE);
	g_signal_connect(area,"drag-begin",G_CALLBACK(begin),NULL);
	g_signal_connect(area,"drag-data-get",G_CALLBACK(provide),NULL);
	g_signal_connect(area,"drag-data-delete",G_CALLBACK(remove_source),NULL);
	g_signal_connect(area,"drag-failed",G_CALLBACK(failure),NULL);
	g_signal_connect(area,"drag-end",G_CALLBACK(end),NULL);
	gtk_widget_show_all(window);display=GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
	g_timeout_add(50,drive,NULL);gtk_main();
	XTestFakeButtonEvent(display,1,False,0);XFlush(display);
	gtk_widget_destroy(window);g_free(payload);
	if(!finished||deleted){fprintf(stderr,"native drag failed: started=%d finished=%d deleted=%d\n",drag!=NULL,finished,deleted);return 4;}
	return 0;
}
