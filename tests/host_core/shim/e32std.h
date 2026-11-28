#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef int32_t TInt; typedef int32_t TInt32; typedef int16_t TInt16; typedef int8_t TInt8;
typedef uint32_t TUint32; typedef uint16_t TUint16; typedef uint8_t TUint8; typedef unsigned int TUint;
typedef int TBool; typedef int64_t TInt64; typedef uint64_t TUint64;
#define ETrue 1
#define EFalse 0
#define NULL_ 0
const TInt KErrNone=0, KErrArgument=-6, KErrTooBig=-40, KErrCorrupt=-20, KErrNotFound=-1, KErrNoMemory=-4, KErrEof=-25;
struct TDesC { const TDesC& Left(int) const { return *this; } };
template<int N> struct TBuf8 { TUint8 d[N]; int len=0; void Zero(){len=0;} };
struct TPtr8 { TPtr8(TUint8* p,int l,int m):p(p),max(m){} TUint8* p; int max; int Length() const {return 0;} };
namespace Mem { inline void FillZ(void* p, int n){ memset(p,0,n);} }
const int EFileRead=1, EFileShareReadersOnly=2;
struct RFs { int Connect(){return -1;} void Close(){} };
struct RFile { int Open(RFs&,const TDesC&,int){return -1;} void Close(){} int Size(int&){return -1;} int Read(int,TPtr8&,int){return -1;} };

#include <string>
#define _L(x) TPtrC(x)
#define _LIT(n,s) static const TPtrC n(s)
const TInt KErrNotReady=-18;
struct TPoint{ TInt iX=0,iY=0; TPoint(){} TPoint(TInt x,TInt y):iX(x),iY(y){} };
struct TSize{ TInt iWidth=0,iHeight=0; TSize(){} TSize(TInt w,TInt h):iWidth(w),iHeight(h){} };
struct TRect{ TPoint iTl,iBr; TRect(){} TRect(TPoint a,TPoint b):iTl(a),iBr(b){} TRect(TPoint a,TSize s):iTl(a),iBr(a.iX+s.iWidth,a.iY+s.iHeight){}
  TSize Size() const {return TSize(iBr.iX-iTl.iX,iBr.iY-iTl.iY);} TInt Width() const{return iBr.iX-iTl.iX;} TInt Height() const{return iBr.iY-iTl.iY;} };
struct TRgb{ TRgb(int,int,int){} TRgb(){} };
static TRgb KRgbBlack, KRgbWhite;
struct TPtrC: TDesC { std::string s; TPtrC(const char* c=""):s(c){} int Length() const{return s.size();} TPtrC Left(int n) const{return TPtrC(s.substr(0,n).c_str());} };
template<int N> struct TBuf{ std::string s; int Length() const{return s.size();} int MaxLength() const{return N;} void Copy(const TPtrC& p){s=p.s;} void Copy(const TDesC&){} void AppendNum(int v){s+=std::to_string(v);} operator const TDesC&() const{static TDesC d;return d;} };
struct CFont{};
struct CGraphicsContext{ enum TBrushStyle{ESolidBrush}; enum TPenStyle{ESolidPen,ENullPen}; enum TTextAlign{ECenter}; };
struct CFbsBitmap{ int Create(TSize,int){return 0;} void LockHeap(){} void UnlockHeap(){} void* DataAddress(){return mem;} int DataStride(){return 960;} TUint32 mem[240*160]; };
struct RFbsSession{ static RFbsSession* GetSession(){return 0;} static int Connect(){return 0;} static void Disconnect(){} };
const int EColor16MU=1;
struct CWindowGc: CGraphicsContext{ void SetBrushStyle(TBrushStyle){} void SetBrushColor(TRgb){} void SetPenColor(TRgb){} void SetPenStyle(TPenStyle){} void Clear(const TRect&){} void DrawRect(const TRect&){}
  void DrawBitmap(const TRect&,const CFbsBitmap*){} void DrawText(const TDesC&,const TRect&,int,TTextAlign){} void DrawText(const TDesC&,const TPoint&){} void UseFont(CFont*){} void DiscardFont(){} };
