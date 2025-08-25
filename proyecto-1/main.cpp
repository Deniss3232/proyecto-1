#include "raylib.h"
#include <vector>
#include <cmath>
#include <string>

// CONFIGURACION
const int ANCHO = 1024;
const int ALTO  = 640;

const float FOV  = 0.66f;
const float VEL  = 4.5f;
const float ROT  = 2.0f;
const float SENS = 0.0028f;

const int TILE_LLAVE  = 8;  
const int TILE_SALIDA = 9;  

enum Estado { MENU, JUEGO, GANASTE };

struct Nivel { int w,h; std::vector<int> cel; };
inline int id(const Nivel& n, int x, int y){ return y*n.w + x; }

inline bool esSolida(const Nivel& n, int x, int y){
    if (x<0||y<0||x>=n.w||y>=n.h) return true;
    int v = n.cel[id(n,x,y)];
    return (v>0 && v!=TILE_SALIDA && v!=TILE_LLAVE);
}


// ANTORCHA 

struct Antorcha {
    RenderTexture2D rt[4];
    void init(int w=32, int h=48){
        for (int i=0;i<4;i++){
            rt[i] = LoadRenderTexture(w,h);
            BeginTextureMode(rt[i]);
            ClearBackground(BLANK);
            DrawRectangle(w/2-2, h-14, 4, 14, Color{80,60,30,255});
            float k = 1.0f + 0.06f*i;
            DrawCircle(w/2, h-18, 10*k, ORANGE);
            DrawCircle(w/2, h-26,  8*k, YELLOW);
            DrawCircle(w/2, h-34,  5*k, GOLD);
            EndTextureMode();
        }
    }
    void draw(int x, int y, float t){
        int f = (int)(t*8) % 4;
        Rectangle src{0,0,(float)rt[f].texture.width, -(float)rt[f].texture.height};
        Rectangle dst{(float)x,(float)y,(float)rt[f].texture.width,(float)rt[f].texture.height};
        DrawTexturePro(rt[f].texture, src, dst, Vector2{0,0}, 0, WHITE);
    }
    void unload(){ for (int i=0;i<4;i++) UnloadRenderTexture(rt[i]); }
};

// TEXTURAS 
static Texture2D GenBrickTexture(int W=128, int H=128){
    Image img = GenImageColor(W,H, Color{170,60,50,255}); // base ladrillo
    for (int y=0; y<H; y+=16) ImageDrawRectangle(&img, 0, y, W, 3, Color{210,200,190,255});      
    for (int y=0; y<H; y+=32){                                                                  
        for (int x=0; x<W; x+=32){
            ImageDrawRectangle(&img, x, y, 3, 13, Color{210,200,190,255});
            ImageDrawRectangle(&img, x+16, y+16, 3, 13, Color{210,200,190,255});
        }
    }
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) if ((x*y + x*13 + y*7) % 97 == 0) ImageDrawPixel(&img, x,y, Color{10,10,10,30});
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}
static Texture2D GenStoneTexture(int W=128, int H=128){
    Image img = GenImageColor(W,H, Color{120,120,130,255});
    // piedras 
    for (int i=0;i<200;i++){
        int cx = GetRandomValue(0,W-1), cy = GetRandomValue(0,H-1);
        int r  = GetRandomValue(6,16);
        Color c = Color{(unsigned char)GetRandomValue(100,140),(unsigned char)GetRandomValue(100,140),(unsigned char)GetRandomValue(110,150),255};
        ImageDrawCircle(&img, cx,cy,r,c);
    }
    // líneas de junta
    for (int y=0; y<H; y+=32) ImageDrawRectangle(&img, 0, y, W, 2, Color{90,90,95,255});
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}
static Texture2D GenMetalTexture(int W=128, int H=128){
    Image img = GenImageColor(W,H, Color{150,155,160,255});
    for (int y=0;y<H;y++) for (int x=0;x<W;x++){
        unsigned char n = (unsigned char)((x*5 + y*3) % 20);
        Color px = Color{(unsigned char)(150+n),(unsigned char)(155+n),(unsigned char)(160+n),255};
        ImageDrawPixel(&img, x,y, px);
    }
    // remaches
    for (int y=16; y<H; y+=32) for (int x=16; x<W; x+=32) ImageDrawCircle(&img, x,y,3, DARKGRAY);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}


// TEXTURAS DE MURO

struct AtlasMuros {
    std::vector<Texture2D> tex;

    void cargar(){
        tex.clear();
        for (int i=1;i<=8;i++){
            std::string path = "assets/wall_" + std::to_string(i) + ".png";
            if (FileExists(path.c_str())){
                Image img = LoadImage(path.c_str());
                tex.push_back(LoadTextureFromImage(img));
                UnloadImage(img);
            }
        }
        if (tex.empty()){
            tex.push_back(GenBrickTexture(128,128)); // 0
            tex.push_back(GenStoneTexture(128,128)); // 1
            tex.push_back(GenMetalTexture(128,128)); // 2
        }
    }

    Texture2D& paraTile(int tile){
        int n = (int)tex.size();
        // Mapeo 
        int idx = 0;
        if (n==1) idx = 0;
        else if (tile>=1) idx = (tile-1) % n;
        return tex[idx];
    }

    void unload(){
        for (auto &t : tex) UnloadTexture(t);
        tex.clear();
    }
};

int main(){
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(ANCHO, ALTO, "Raycaster UVG - Llave + Texturas por Tile");
    SetTargetFPS(60);

    // Audio
    InitAudioDevice();
    Music music{}; Sound sStep{}, sWin{}, sPick{}, sNeed{};
    bool haveMusic=false, haveStep=false, haveWin=false, havePick=false, haveNeed=false;

    if (FileExists("assets/music.ogg"))      { music = LoadMusicStream("assets/music.ogg"); haveMusic=true; }
    else if (FileExists("assets/music.mp3")) { music = LoadMusicStream("assets/music.mp3"); haveMusic=true; }
    else if (FileExists("assets/music.wav")) { music = LoadMusicStream("assets/music.wav"); haveMusic=true; }
    if (haveMusic){ SetMusicVolume(music, 0.55f); PlayMusicStream(music); }

    if (FileExists("assets/step.wav"))  { sStep  = LoadSound("assets/step.wav");  haveStep=true;  SetSoundVolume(sStep,  0.85f); }
    if (FileExists("assets/win.wav"))   { sWin   = LoadSound("assets/win.wav");   haveWin=true;   SetSoundVolume(sWin,   0.95f); }
    if (FileExists("assets/pick.wav"))  { sPick  = LoadSound("assets/pick.wav");  havePick=true;  SetSoundVolume(sPick,  0.90f); }
    if (FileExists("assets/need.wav"))  { sNeed  = LoadSound("assets/need.wav");  haveNeed=true;  SetSoundVolume(sNeed,  0.90f); }

    // TEXTURAS 
    AtlasMuros atlas; atlas.cargar();

    Antorcha ant; ant.init();

   

    // NIVELES
    
    std::vector<int> nivelA = {
      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
      1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,1,
      1,0,1,1,0,1,1,1,0,2,0,1,1,1,0,1,
      1,0,1,0,0,0,0,1,0,2,0,1,0,0,0,1,
      1,0,1,0,1,1,0,1,0,2,0,1,0,1,0,1,
      1,0,0,0,0,0,0,0,0,2,0,0,0,1,0,1,
      1,0,1,1,1,1,1,1,0,2,1,1,0,1,0,1,
      1,0,1,0,0,0,0,1,0,2,0,1,0,1,0,1,
      1,0,1,0,1,1,0,1,0,2,0,1,0,1,0,1,
      1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,1,
      1,0,1,1,1,1,1,1,1,2,1,1,1,1,0,1,
      1,0,0,0,0,0,0,0,0,2,0,0,0,1,0,1,
      1,0,1,1,1,1,1,1,0,2,1,1,0,1,0,1,
      1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,1,
      1,0,0,0,0,0,0,0,0,0,8,0,0,0,9,1, 
      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
    };
    std::vector<int> nivelB = {
      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
      1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,
      1,0,1,1,0,1,0,1,0,1,0,1,0,1,0,1,
      1,0,1,0,0,1,0,0,0,1,0,0,0,1,0,1,
      1,0,1,0,1,1,1,1,0,1,1,1,0,1,0,1,
      1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,
      1,1,1,1,1,1,0,1,1,1,0,1,1,1,0,1,
      1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,1,
      1,0,1,1,0,1,1,1,0,1,1,1,0,1,0,1,
      1,0,1,0,0,0,0,1,0,0,0,1,0,1,0,1,
      1,0,1,0,1,1,0,1,0,1,0,1,0,1,0,1,
      1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,
      1,1,1,1,1,1,0,1,1,1,0,1,1,1,0,1,
      1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,
      1,0,0,0,0,0,8,0,0,0,0,0,0,0,9,1, 
      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
    };

    // JUGADOR
    Nivel m{16,16,{}};
    int nivelSel = 1;
    Vector2 pos = {1.5f, 1.5f};
    float dirX=1.0f, dirY=0.0f, plX=0.0f, plY=FOV;

    Estado estado = MENU;
    bool cursorMostrado = true;
    bool haveKey = false;         // ESTADO LLAVE
    float stepTimer = 0.0f;

    // Mensajes
    float msgTimer=0.0f; const char* msgTxt="";
    auto setMsg = [&](const char* t, float sec){ msgTxt=t; msgTimer=sec; };

    while(!WindowShouldClose()){
        float dt = GetFrameTime();
        if (haveMusic) UpdateMusicStream(music);
        if (msgTimer>0.0f) msgTimer -= dt;

        // menu
        if (estado==MENU){
            if (!cursorMostrado){ ShowCursor(); cursorMostrado=true; }
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText("RAYCASTER UVG ", 60, 70, 36, RAYWHITE);
            DrawText("Toma la LLAVE .", 60, 130, 22, LIGHTGRAY);
            DrawText("W/S: Adelante/Atras  A/D: Strafe  Mouse o <- ->: Girar", 60, 160, 22, GRAY);
            DrawText("Nivel (1/2)", 60, 210, 26, LIGHTGRAY);
            DrawText((nivelSel==1)?"Actual: Nivel 1":"Actual: Nivel 2", 60, 240, 26, (nivelSel==1)?YELLOW:ORANGE);
            DrawText("ENTER para jugar", 60, 290, 28, YELLOW);
            ant.draw(60, 340, GetTime());
            DrawFPS(ANCHO-90,10);
            EndDrawing();

            if (IsKeyPressed(KEY_ONE)) nivelSel=1;
            if (IsKeyPressed(KEY_TWO)) nivelSel=2;
            if (IsKeyPressed(KEY_ENTER)){
                m.cel = (nivelSel==1)? nivelA : nivelB;
                estado=JUEGO; HideCursor(); cursorMostrado=false;
                pos={1.5f,1.5f}; dirX=1.0f; dirY=0.0f; plX=0.0f; plY=FOV;
                haveKey=false; setMsg((nivelSel==1)?"Nivel 1":"Nivel 2", 1.0f);
            }
            continue;
        }

        // ganaste
        if (estado==GANASTE){
            if (!cursorMostrado){ ShowCursor(); cursorMostrado=true; }
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText("¡GANASTE!", 60, 80, 60, YELLOW);
            DrawText("ENTER: Menu", 60, 160, 28, RAYWHITE);
            ant.draw(60,210,GetTime());
            EndDrawing();
            if (IsKeyPressed(KEY_ENTER)) estado=MENU;
            continue;
        }

        // juego
        if (cursorMostrado){ HideCursor(); cursorMostrado=false; }

        // Girar
        Vector2 md = GetMouseDelta();
        float ang = -md.x*SENS;
        float c=cosf(ang), s=sinf(ang);
        float odx=dirX, opx=plX;
        dirX = odx*c - dirY*s;  dirY = odx*s + dirY*c;
        plX  = opx*c - plY*s;   plY  = opx*s + plY*c;
        if (IsKeyDown(KEY_RIGHT)){
            float a=-ROT*dt, cc=cosf(a), ss=sinf(a);
            float tx=dirX*cc - dirY*ss, ty=dirX*ss + dirY*cc;
            float px=plX*cc  - plY*ss,  py=plX*ss  + plY*cc;
            dirX=tx; dirY=ty; plX=px; plY=py;
        }
        if (IsKeyDown(KEY_LEFT)){
            float a=ROT*dt, cc=cosf(a), ss=sinf(a);
            float tx=dirX*cc - dirY*ss, ty=dirX*ss + dirY*cc;
            float px=plX*cc  - plY*ss,  py=plX*ss  + plY*cc;
            dirX=tx; dirY=ty; plX=px; plY=py;
        }

        // Movimiento
        Vector2 d{0,0};
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))   { d.x+=dirX*VEL*dt; d.y+=dirY*VEL*dt; }
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) { d.x-=dirX*VEL*dt; d.y-=dirY*VEL*dt; }
        if (IsKeyDown(KEY_A)) { d.x+=(-dirY)*VEL*dt; d.y+=( dirX)*VEL*dt; }
        if (IsKeyDown(KEY_D)) { d.x+=( dirY)*VEL*dt; d.y+=(-dirX)*VEL*dt; }

        float nx=pos.x+d.x, ny=pos.y+d.y; bool moved=false;
        if (!esSolida(m,(int)nx,(int)pos.y)) { pos.x=nx; moved=true; }
        if (!esSolida(m,(int)pos.x,(int)ny)) { pos.y=ny; moved=true; }
        if (moved){ stepTimer+=dt; if (haveStep && stepTimer>0.25f){ PlaySound(sStep); stepTimer=0.0f; } }
        else stepTimer=0.2f;

        // RECOGER LLAVE
        {
            int cx=(int)pos.x, cy=(int)pos.y;
            int &tile = m.cel[id(m,cx,cy)];
            if (tile==TILE_LLAVE){
                tile=0; haveKey=true;
                if (havePick) PlaySound(sPick);
                setMsg("Llave obtenida",1.2f);
            }
        }

        // SALIDA REQUIERE LLAVE
        if (m.cel[id(m,(int)pos.x,(int)pos.y)]==TILE_SALIDA){
            if (haveKey){
                if (haveWin) PlaySound(sWin);
                estado=GANASTE; continue;
            } else {
                if (haveNeed) PlaySound(sNeed);
                setMsg("Necesitas la llave",1.0f);
            }
        }

        // RENDER 3D CON TEXTURAS POR TILE
        BeginDrawing();
        ClearBackground(BLACK);
        int W=GetScreenWidth(), H=GetScreenHeight();

        DrawRectangle(0,0,W,H/2, Color{60,90,150,255});      // cielo
        DrawRectangle(0,H/2,W,H/2, Color{50,40,30,255});     // piso

        for (int x=0; x<W; ++x){
            float camX = 2.0f*x/(float)W - 1.0f;
            float rX = dirX + plX*camX;
            float rY = dirY + plY*camX;

            int mx=(int)pos.x, my=(int)pos.y;
            float dX = (rX==0)?1e30f:fabsf(1.0f/rX);
            float dY = (rY==0)?1e30f:fabsf(1.0f/rY);

            float sX, sY; int stX, stY;
            if (rX<0){ stX=-1; sX=(pos.x-mx)*dX; } else { stX=1; sX=(mx+1.0f-pos.x)*dX; }
            if (rY<0){ stY=-1; sY=(pos.y-my)*dY; } else { stY=1; sY=(my+1.0f-pos.y)*dY; }

            bool hit=false, sideY=false;
            int tileHit = 0;
            while(!hit){
                if (sX < sY){ sX+=dX; mx+=stX; sideY=false; }
                else        { sY+=dY; my+=stY; sideY=true;  }
                if (mx<0||my<0||mx>=m.w||my>=m.h){ hit=true; }
                else {
                    int v=m.cel[id(m,mx,my)];
                    if (v>0 && v!=TILE_LLAVE && v!=TILE_SALIDA){ hit=true; tileHit=v; }
                }
            }

            float perp = (!sideY)? (sX-dX) : (sY-dY);
            if (perp < 0.0001f) perp = 0.0001f;
            int h = (int)(H/perp);
            int y0 = -h/2 + H/2;

            // Coordenada X sobre la pared 
            float wallX = (!sideY)? (pos.y + perp*rY) : (pos.x + perp*rX);
            wallX -= floorf(wallX);

            Texture2D &tex = atlas.paraTile(tileHit);
            const int tw = tex.width, th = tex.height;

            int tx = (int)(wallX * tw);
            if (!sideY && rX>0) tx = tw - tx - 1;
            if ( sideY && rY<0) tx = tw - tx - 1;

            // Dibuja una columna 
            Rectangle src{(float)tx, 0.0f, 1.0f, (float)th};
            Rectangle dst{(float)x, (float)y0, 1.0f, (float)h};
            Color tint = sideY ? Color{210,210,210,255} : WHITE; 
            DrawTexturePro(tex, src, dst, Vector2{0,0}, 0.0f, tint);
        }

        // Minimapa 
        int tam=10, ox=10, oy=10;
        for (int y=0;y<m.h;y++){
            for (int x2=0;x2<m.w;x2++){
                int v=m.cel[id(m,x2,y)];
                Color c = (v==0)? Color{25,25,30,255} :
                          (v==TILE_LLAVE)?  Color{80,220,220,255} :
                          (v==TILE_SALIDA)? Color{240,240,120,255} :
                                            Color{120,120,120,255};
                DrawRectangle(ox+x2*tam, oy+y*tam, tam, tam, c);
            }
        }
        DrawCircle(ox+(int)(pos.x*tam), oy+(int)(pos.y*tam), 3, RED);
        DrawLine(ox+(int)(pos.x*tam), oy+(int)(pos.y*tam),
                 ox+(int)((pos.x+dirX*0.8f)*tam), oy+(int)((pos.y+dirY*0.8f)*tam), RED);

        // HUD LLAVE
        DrawText(haveKey? "Llave: SI" : "Llave: NO", 20, H-40, 24, haveKey? GREEN : GRAY);

        // Mensajes
        if (msgTimer>0.0f){
            int fw = MeasureText(msgTxt, 40);
            DrawRectangle(ANCHO/2 - fw/2 - 12, 18, fw+24, 52, Color{0,0,0,140});
            DrawText(msgTxt, ANCHO/2 - fw/2, 24, 40, YELLOW);
        }

        ant.draw(ANCHO-80, 20, GetTime());
        DrawFPS(W-90,10);
        EndDrawing();
    }

    // Limpieza
    ant.unload();
    if (haveMusic){ StopMusicStream(music); UnloadMusicStream(music); }
    if (haveStep) UnloadSound(sStep);
    if (haveWin)  UnloadSound(sWin);
    if (havePick) UnloadSound(sPick);
    if (haveNeed) UnloadSound(sNeed);
    atlas.unload();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
