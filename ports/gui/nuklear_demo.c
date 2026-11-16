/* Nuklear command renderer on an explicitly delegated slot-bound pixel region.
 * Enter activates the focused button; q + Enter returns to the shell. */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <plan2001/surface.h>
#include <plan2001/vobj.h>
extern long objopen(unsigned long long,void *,long,unsigned long long,void *);
extern long objinvoke(int,void *,long);
extern long objclose(int);
static uint32_t *pixels;
static int width,height,clipx,clipy,clipw,cliph;
static unsigned char *coverage;
static int atlasw,atlash;
static struct nk_font *font;
static void pixel(int x,int y,struct nk_color c,unsigned alpha)
{
    if(x<0 || y<0 || x>=width || y>=height || x<clipx || y<clipy || x>=clipx+clipw || y>=clipy+cliph)return;
    unsigned a=alpha*c.a/255;
    uint32_t old=pixels[y*width+x];
    unsigned r=(c.r*a+((old>>16)&255)*(255-a))/255;
    unsigned g=(c.g*a+((old>>8)&255)*(255-a))/255;
    unsigned b=(c.b*a+(old&255)*(255-a))/255;
    pixels[y*width+x]=(r<<16)|(g<<8)|b;
}
static void rect(int x,int y,int w,int h,struct nk_color c)
{
    int x0=x>clipx?x:clipx,y0=y>clipy?y:clipy;
    int x1=x+w<clipx+clipw?x+w:clipx+clipw,y1=y+h<clipy+cliph?y+h:clipy+cliph;
    if(x0<0)x0=0;if(y0<0)y0=0;if(x1>width)x1=width;if(y1>height)y1=height;
    for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++)pixel(xx,yy,c,255);
}
static void line(int x0,int y0,int x1,int y1,struct nk_color color)
{
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
    for(;;){pixel(x0,y0,color,255);if(x0==x1 && y0==y1)break;
        int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}
}
static void text(const struct nk_command_text *t)
{
    float at=t->x,scale=t->height/font->info.height;
    rect(t->x,t->y,t->w,t->h,t->background);
    for(int i=0;i<t->length;){
        nk_rune rune;int used=nk_utf_decode(t->string+i,&rune,t->length-i);if(used<=0)break;i+=used;
        const struct nk_font_glyph *g=nk_font_find_glyph(font,rune);if(!g)continue;
        int x0=(int)(at+g->x0*scale),y0=(int)(t->y+g->y0*scale);
        int w=(int)((g->x1-g->x0)*scale),h=(int)((g->y1-g->y0)*scale);
        int u=(int)(g->u0*atlasw),v=(int)(g->v0*atlash);
        int sw=(int)((g->u1-g->u0)*atlasw+0.5f),sh=(int)((g->v1-g->v0)*atlash+0.5f);
        if(w>0 && h>0 && sw>0 && sh>0)for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
            int ax=u+xx*sw/w,ay=v+yy*sh/h;
            if(ax>=0 && ay>=0 && ax<atlasw && ay<atlash)pixel(x0+xx,y0+yy,t->foreground,coverage[ay*atlasw+ax]);
        }
        at+=g->xadvance*scale;
    }
}
static int render(struct nk_context *ctx)
{
    clipx=clipy=0;clipw=width;cliph=height;
    for(int i=0;i<width*height;i++)pixels[i]=0x142033;
    const struct nk_command *cmd;
    nk_foreach(cmd,ctx){switch(cmd->type){
    case NK_COMMAND_NOP:break;
    case NK_COMMAND_SCISSOR:{const struct nk_command_scissor *c=(const void*)cmd;
        clipx=c->x;clipy=c->y;clipw=c->w;cliph=c->h;break;}
    case NK_COMMAND_RECT_FILLED:{const struct nk_command_rect_filled *c=(const void*)cmd;rect(c->x,c->y,c->w,c->h,c->color);break;}
    case NK_COMMAND_RECT:{const struct nk_command_rect *c=(const void*)cmd;
        line(c->x,c->y,c->x+c->w-1,c->y,c->color);line(c->x,c->y+c->h-1,c->x+c->w-1,c->y+c->h-1,c->color);
        line(c->x,c->y,c->x,c->y+c->h-1,c->color);line(c->x+c->w-1,c->y,c->x+c->w-1,c->y+c->h-1,c->color);break;}
    case NK_COMMAND_LINE:{const struct nk_command_line *c=(const void*)cmd;line(c->begin.x,c->begin.y,c->end.x,c->end.y,c->color);break;}
    case NK_COMMAND_TEXT:text((const void*)cmd);break;
    default:fprintf(stderr,"unsupported Nuklear command %d\n",cmd->type);return -1;
    }}
    nk_clear(ctx);return p9_surface_present();
}
static int deny_checks(int handle)
{
    /* A SPAWN child has no ambient session factory or slot selector. */
    if(objopen(ObjOpenConsoleSurface,NULL,0,0,NULL)>=0)return -1;
    ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpSurfacePresent;c.arg0=1;
    if(objinvoke(handle,&c,sizeof c)>=0)return -1;
    ObjOpenHandleReq request={handle,0};
    int ro=objopen(ObjOpenHandle,&request,sizeof request,Vread|Vmeta|Vdelegate,NULL);
    if(ro<=0)return -1;
    c.arg0=0;int denied=objinvoke(ro,&c,sizeof c)<0;objclose(ro);
    return denied?0:-1;
}
int main(void)
{
    ObjSurfaceInfo info;int handle=p9_surface_handle();
    if(handle<=0 || deny_checks(handle)){fprintf(stderr,"GUI surface authority denied\n");return 1;}
    pixels=p9_surface_map(&info);
    if(!pixels || info.width<700 || info.height<500)return 2;
    width=(int)info.width;height=(int)info.height;
    struct nk_font_atlas atlas;struct nk_context ctx;
    nk_font_atlas_init_default(&atlas);nk_font_atlas_begin(&atlas);
    font=nk_font_atlas_add_default(&atlas,24,NULL);
    const void *image=nk_font_atlas_bake(&atlas,&atlasw,&atlash,NK_FONT_ATLAS_ALPHA8);
    if(!font || !image)return 3;
    coverage=malloc((size_t)atlasw*atlash);if(!coverage)return 3;
    memcpy(coverage,image,(size_t)atlasw*atlash);nk_font_atlas_end(&atlas,nk_handle_id(1),NULL);
    if(!nk_init_default(&ctx,&font->handle))return 4;
    struct nk_rect button=nk_rect(0,0,0,0);int approved=0,click=0,quit=0;
    printf("GUI-READY slot=%u\n",info.slot);fflush(stdout);
    while(!quit){
        nk_input_begin(&ctx);
        if(click){
            int x=(int)(button.x+button.w/2),y=(int)(button.y+button.h/2);
            nk_input_motion(&ctx,x,y);
            nk_input_button(&ctx,NK_BUTTON_LEFT,x,y,click==1);
        }
        nk_input_end(&ctx);
        if(nk_begin(&ctx,"Plan2001 / Nuklear",nk_rect(40,48,620,380),NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_NO_SCROLLBAR)){
            nk_layout_row_dynamic(&ctx,40,1);nk_label(&ctx,"Native graphics on this session's surface",NK_TEXT_LEFT);
            nk_layout_row_dynamic(&ctx,64,1);
            struct nk_rect status=nk_widget_bounds(&ctx);
            nk_spacer(&ctx);
            struct nk_color status_color=approved?nk_rgb(20,150,70):nk_rgb(49,95,165);
            const char *message=approved?"Approved":"Ready for approval";
            nk_fill_rect(nk_window_get_canvas(&ctx),status,0,status_color);
            nk_draw_text(nk_window_get_canvas(&ctx),nk_rect(status.x+16,status.y+16,status.w-32,32),
                message,(int)strlen(message),&font->handle,status_color,nk_rgb(255,255,255));
            nk_layout_row_dynamic(&ctx,64,1);button=nk_widget_bounds(&ctx);
            if(nk_button_label(&ctx,"[Enter] Approve")){approved=1;puts("GUI-APPROVED");fflush(stdout);}
            nk_layout_row_dynamic(&ctx,40,1);nk_label(&ctx,"q + Enter: return to the shell",NK_TEXT_LEFT);
        }
        nk_end(&ctx);
        if(render(&ctx)){quit=1;break;}
        if(click==1){click=2;continue;}
        if(click==2){click=0;continue;}
        char input[64];int n=read(0,input,sizeof input);
        if(n<0 && errno==EINTR)continue;if(n<=0)break;
        for(int i=0;i<n;i++)if(input[i]=='q')quit=1;else if(input[i]=='\n')click=1;
    }
    nk_free(&ctx);nk_font_atlas_clear(&atlas);free(coverage);
    if(p9_surface_unmap())return 5;
    puts("GUI-EXIT");return 0;
}
