"""Orthographic front / side / top shaded preview of a decoded mesh (PIL only): render(V, F) -> PIL image. Used to eyeball level_mesh.decode output."""
import numpy as np
from PIL import Image,ImageDraw
def render(V,F,size=360):
    out=[]
    c=(V.max(0)+V.min(0))/2; r=(V.max(0)-V.min(0)).max()/2+1e-6
    views=[('front',(0,2),1),('side',(1,2),0),('top',(0,1),2)]
    for nm,(a,b),dep in views:
        im=Image.new('RGB',(size,size),(30,30,40)); dr=ImageDraw.Draw(im)
        T=V[F]; n=np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]); ln=np.linalg.norm(n,axis=1)+1e-12; n/=ln[:,None]
        order=np.argsort(T[:,:,dep].mean(1))
        L=np.array([0.4,-0.5,0.8]);L/=np.linalg.norm(L)
        for i in order:
            p=[((T[i,k,a]-c[a])/r*0.46+0.5)*size for k in range(3)],[(1-((T[i,k,b]-c[b])/r*0.46+0.5))*size for k in range(3)]
            sh=int(60+150*abs(n[i]@L))
            dr.polygon(list(zip(*p)),fill=(sh,sh,sh))
        out.append(im)
    W=Image.new('RGB',(size*3,size)); [W.paste(o,(i*size,0)) for i,o in enumerate(out)]
    return W
