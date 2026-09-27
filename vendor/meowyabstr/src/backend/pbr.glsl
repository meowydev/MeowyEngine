vec3 safeNormal(vec3 v) {return v/sqrt(max(dot(v,v),0.0000001));}
vec3 shadePBR(vec3 base,vec3 n,vec3 world,vec3 eye,vec3 direction,vec3 radiance,vec3 ambient,vec3 emission,float metal,float rough,float ao) {
    const float pi=3.14159265359;
    n=safeNormal(n); vec3 v=safeNormal(eye-world),l=safeNormal(-direction),h=safeNormal(v+l);
    float nv=max(dot(n,v),0.0001),nl=max(dot(n,l),0.0),nh=max(dot(n,h),0.0),hv=max(dot(h,v),0.0);
    rough=clamp(rough,0.045,1.0); metal=clamp(metal,0.0,1.0);
    float a=rough*rough,a2=a*a,d=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/max(pi*d*d,0.000001),k=(rough+1.0)*(rough+1.0)/8.0;
    float geometry=(nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));
    vec3 f0=mix(vec3(0.04),base,metal);
    vec3 fresnel=f0+(vec3(1.0)-f0)*pow(1.0-hv,5.0);
    vec3 specular=distribution*geometry*fresnel/max(4.0*nv*nl,0.0001);
    vec3 diffuse=(vec3(1.0)-fresnel)*(1.0-metal)*base/pi;
    vec3 color=(diffuse+specular)*radiance*nl+ambient*base*ao+emission;
    color=max(color,vec3(0.0)); color=color/(color+vec3(1.0));
    return pow(color,vec3(1.0/2.2));
}
// Image-based lighting variant: envDiffuse is the environment sampled along the
// surface normal (irradiance approximation); envSpecular is the environment
// sampled along the reflection vector (roughness-blurred prefilter
// approximation). Replaces the flat ambient term with environment lighting.
vec3 shadePBRIBL(vec3 base,vec3 n,vec3 world,vec3 eye,vec3 direction,vec3 radiance,vec3 emission,float metal,float rough,float ao,vec3 envDiffuse,vec3 envSpecular) {
    const float pi=3.14159265359;
    n=safeNormal(n); vec3 v=safeNormal(eye-world),l=safeNormal(-direction),h=safeNormal(v+l);
    float nv=max(dot(n,v),0.0001),nl=max(dot(n,l),0.0),nh=max(dot(n,h),0.0),hv=max(dot(h,v),0.0);
    rough=clamp(rough,0.045,1.0); metal=clamp(metal,0.0,1.0);
    float a=rough*rough,a2=a*a,dd=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/max(pi*dd*dd,0.000001),k=(rough+1.0)*(rough+1.0)/8.0;
    float geometry=(nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));
    vec3 f0=mix(vec3(0.04),base,metal);
    vec3 fresnel=f0+(vec3(1.0)-f0)*pow(1.0-hv,5.0);
    vec3 specular=distribution*geometry*fresnel/max(4.0*nv*nl,0.0001);
    vec3 diffuse=(vec3(1.0)-fresnel)*(1.0-metal)*base/pi;
    vec3 direct=(diffuse+specular)*radiance*nl;
    // Ambient IBL: Fresnel-Schlick with roughness for the specular reflection.
    vec3 fr=f0+(max(vec3(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);
    vec3 kd=(vec3(1.0)-fr)*(1.0-metal);
    vec3 ambient=(kd*base*envDiffuse+fr*envSpecular)*ao;
    vec3 color=direct+ambient+emission;
    color=max(color,vec3(0.0)); color=color/(color+vec3(1.0));
    return pow(color,vec3(1.0/2.2));
}
// Precomputed split-sum IBL (Karis). irradiance is the cosine-convolved diffuse
// term (already linear), prefiltered is the roughness-prefiltered specular color
// (linear), and brdf is the (scale,bias) pair from the BRDF integration LUT.
// Specular ambient = prefiltered * (F0*brdf.x + brdf.y).
vec3 shadePBRSplitSum(vec3 base,vec3 n,vec3 world,vec3 eye,vec3 direction,vec3 radiance,vec3 emission,float metal,float rough,float ao,vec3 irradiance,vec3 prefiltered,vec2 brdf) {
    const float pi=3.14159265359;
    n=safeNormal(n); vec3 v=safeNormal(eye-world),l=safeNormal(-direction),h=safeNormal(v+l);
    float nv=max(dot(n,v),0.0001),nl=max(dot(n,l),0.0),nh=max(dot(n,h),0.0),hv=max(dot(h,v),0.0);
    rough=clamp(rough,0.045,1.0); metal=clamp(metal,0.0,1.0);
    float a=rough*rough,a2=a*a,dd=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/max(pi*dd*dd,0.000001),k=(rough+1.0)*(rough+1.0)/8.0;
    float geometry=(nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));
    vec3 f0=mix(vec3(0.04),base,metal);
    vec3 fresnel=f0+(vec3(1.0)-f0)*pow(1.0-hv,5.0);
    vec3 specular=distribution*geometry*fresnel/max(4.0*nv*nl,0.0001);
    vec3 diffuse=(vec3(1.0)-fresnel)*(1.0-metal)*base/pi;
    vec3 direct=(diffuse+specular)*radiance*nl;
    // Ambient: diffuse from irradiance, specular from prefiltered * BRDF LUT.
    vec3 fr=f0+(max(vec3(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);
    vec3 kd=(vec3(1.0)-fr)*(1.0-metal);
    vec3 ambientDiffuse=kd*base*irradiance;
    vec3 ambientSpecular=prefiltered*(f0*brdf.x+vec3(brdf.y));
    vec3 color=direct+(ambientDiffuse+ambientSpecular)*ao+emission;
    color=max(color,vec3(0.0)); color=color/(color+vec3(1.0));
    return pow(color,vec3(1.0/2.2));
}
