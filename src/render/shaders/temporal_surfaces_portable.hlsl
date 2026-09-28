StructuredBuffer<uint> sourcePixels : register(t0,space0);
StructuredBuffer<float4> sourceSurfaces : register(t1,space0);
StructuredBuffer<float> sourceDepth : register(t2,space0);
StructuredBuffer<uint> protectedPixels : register(t3,space0);
RWStructuredBuffer<uint> targetPixels : register(u0,space1);
RWStructuredBuffer<float4> targetSurfaces : register(u1,space1);
cbuffer Settings : register(b0,space2) {uint width,height;float2 jitter;};
bool eligible(uint value) {
    uint tag=(value>>8)&255;
    return tag==0 || tag==3 || tag==4 || tag==5;
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID) {
    uint2 p=id.xy;if(p.x>=width || p.y>=height) return;
    uint i=p.y*width+p.x;
    // HUD restoration happens after temporal colour resolution. Its ownership
    // must win here too, even if a jittered model shares the same palette entry.
    if(((protectedPixels[i]>>8)&255)==1) {
        targetPixels[i]=protectedPixels[i]&~0x01ff0000u;targetSurfaces[i]=0;return;
    }
    uint selected=i;
    if(eligible(sourcePixels[i])) {
        float2 at=clamp(float2(p)+jitter,0,float2(width-1,height-1));
        uint2 a=uint2(at);float2 f=frac(at);float nearest=3.402823e38,bestWeight=-1;
        for(uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
            uint2 n=min(a+uint2(x,y),uint2(width-1,height-1));uint j=n.y*width+n.x;
            float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y),z=sourceDepth[j];
            // Choose one contributing foreground surface; averaging normals
            // or palette owners across silhouettes produces false lighting.
            // Match the motion-guide resolve on coplanar surfaces as well:
            // a tiny first tap must not steal a stronger tap's normal/owner.
            if(weight>0 && eligible(sourcePixels[j]) && isfinite(z) && z>0
                && (z<nearest || (z==nearest && weight>bestWeight))) {
                nearest=z;selected=j;bestWeight=weight;
            }
        }
    }
    uint packed=sourcePixels[selected];float4 surface=sourceSurfaces[selected];
    bool valid=eligible(packed) && (packed&0x01000000u)!=0
        && ((packed>>16)&255)==(packed&255) && all(isfinite(surface)) && surface.w>0;
    targetPixels[i]=valid?packed:packed&~0x01ff0000u;
    targetSurfaces[i]=valid?surface:0;
}
