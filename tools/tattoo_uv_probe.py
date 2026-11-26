import csv, numpy as np, json
from scipy import ndimage
from PIL import Image
d=np.array([[float(x) for x in r] for r in csv.reader(open('_body.csv'))])
T=np.array([[int(x) for x in r] for r in csv.reader(open('_body.csv.tri'))])
U,V,X,Y,Z=d[:,0],d[:,1],d[:,2],d[:,3],d[:,4]  # the skin texture row = the mesh V (no flip; checked in 3D)
W=1024
PX=np.full((W,W),np.nan,np.float32);PY=PX.copy();PZ=PX.copy();NZ=np.zeros((W,W),np.float32);NY=NZ.copy()
for t in T:
    pts=np.array([[U[i]*W,V[i]*W] for i in t]); mn=np.floor(pts.min(0)).astype(int); mx=np.ceil(pts.max(0)).astype(int)
    if (mx-mn).max()>200: continue
    a,b,c=pts; den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    if abs(den)<1e-9: continue
    P=[np.array([X[i],Y[i],Z[i]]) for i in t]
    n=np.cross(P[1]-P[0],P[2]-P[0]); ln=np.linalg.norm(n); n=n/ln if ln>0 else n
    for py in range(max(mn[1],0),min(mx[1]+1,W)):
        for px in range(max(mn[0],0),min(mx[0]+1,W)):
            w1=((b[1]-c[1])*(px-c[0])+(c[0]-b[0])*(py-c[1]))/den
            w2=((c[1]-a[1])*(px-c[0])+(a[0]-c[0])*(py-c[1]))/den
            if w1>=0 and w2>=0 and 1-w1-w2>=0:
                p=w1*P[0]+w2*P[1]+(1-w1-w2)*P[2]
                PX[py,px],PY[py,px],PZ[py,px]=p; NZ[py,px]=n[2]; NY[py,px]=n[1]
s0=np.sign(np.nanmean(NZ[int(.30*W):int(.38*W),int(.48*W):int(.52*W)]))
print('front normal sign',s0)
front=(NZ*s0)>0.3; back=(NZ*s0)<-0.3
AX=np.abs(PX); valid=~np.isnan(PY)
def reg(name,xr,yr,face):
    m=valid&(AX>=xr[0])&(AX<xr[1])&(PY>=yr[0])&(PY<yr[1])
    if face=='front': m&=front
    if face=='back': m&=back
    m=ndimage.binary_opening(m,iterations=2)
    dt=ndimage.distance_transform_edt(m)
    if dt.max()<3: return None
    py,px=np.unravel_index(np.argmax(dt),dt.shape)
    return dict(name=name,u=round(px/W,3),v=round(py/W,3),r=round(float(dt.max())/W,3),y=round(float(PY[py,px]),2),x=round(float(PX[py,px]),2))
specs=[('chest_front',(0,0.15),(1.12,1.32),'front'),('chest_back',(0,0.15),(1.12,1.32),'back'),
('abdomen_front',(0,0.15),(0.90,1.10),'front'),('lower_back',(0,0.15),(0.90,1.10),'back'),
('neck_front',(0,0.09),(1.32,1.42),'front'),('neck_back',(0,0.09),(1.32,1.42),'back'),
('shoulder',(0.15,0.24),(1.15,1.45),'any'),
('upper_arm_front',(0.24,0.37),(0.9,1.5),'front'),('upper_arm_back',(0.24,0.37),(0.9,1.5),'back'),
('forearm_front',(0.38,0.55),(0.9,1.5),'front'),('forearm_back',(0.38,0.55),(0.9,1.5),'back'),
('hand',(0.56,0.7),(0.9,1.5),'any'),
('hip_front',(0.0,0.2),(0.82,0.92),'front'),('glutes',(0.0,0.2),(0.82,0.92),'back'),
('thigh_front',(0.0,0.2),(0.55,0.82),'front'),('thigh_back',(0.0,0.2),(0.55,0.82),'back'),
('calf_front',(0.0,0.2),(0.13,0.55),'front'),('calf_back',(0.0,0.2),(0.13,0.55),'back'),
('foot',(0.0,0.2),(0.0,0.13),'any')]
out=[]
for sp in specs:
    r=reg(*sp)
    if r: out.append(r); print(r)
    else: print(sp[0],'-')
json.dump(out,open('_regions4.json','w'),indent=1)
