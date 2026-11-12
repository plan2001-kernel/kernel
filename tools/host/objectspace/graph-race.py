# A child mutates the nested node while the parent snapshots two graph nodes.
# Two separate capability regions avoid sharing a Python heap or writer slot.
import plan9 as p
v=p.obj_open_volume()
_,gid=p.obj_create_ge(v,b'')
p.obj_commit(v)
root=p.space_new()
child=p.space_new()
edge=p.space_bind(root,'child',child,p.VREAD+p.VMETA)
ctl=p.region_create(8192)
progress=p.region_create(8192)
cbase=p.region_map(ctl,0x4f700000)
rbase=p.region_map(progress,0x4f710000,p.OBJ_REGION_MAP_READONLY)
p.region_atomic_store(cbase,0)
pid,_=p.rforkex(p.RFPROC,((child,0,child),(ctl,0,ctl),(progress,0,progress)))
if pid==0:
    cb=p.region_map(ctl,0x4f720000,p.OBJ_REGION_MAP_READONLY)
    rb=p.region_map(progress,0x4f730000)
    binding=0
    n=0
    while p.region_atomic_load(cb)==0:
        if binding:
            p.space_unbind(child,binding)
            binding=0
        else:
            binding=p.space_bind(child,'cycle',child,p.VREAD+p.VMETA)
        n+=1
        p.region_atomic_store(rb,n)
    p.exits('')
p.sleep(100)
before=p.region_atomic_load(rbase)
assert before>0
for i in range(8):
    version=p.space_save(root,v,gid)
    assert version>0
    snapshot=p.space_load(v,gid)
    rows,_,_=p.space_list(snapshot)
    assert len(rows)==1
    nested=p.space_resolve(snapshot,edge,p.VREAD+p.VMETA)
    edges,_,_=p.space_list(nested)
    assert len(edges)==0 or len(edges)==1
    if edges:
        bid,label,kind,rights,relation,ident,flags=next(iter(edges))
        assert label=='cycle' and kind==p.VKIND_OBJECT_SPACE and flags==0
        back=p.space_resolve(nested,bid,p.VREAD+p.VMETA)
        assert p.space_list(back)==p.space_list(nested)
        p.obj_close(back)
    p.obj_close(nested)
    p.obj_close(snapshot)
after=p.region_atomic_load(rbase)
p.region_atomic_store(cbase,1)
status=p.process_wait(pid,-1)
assert status[2]==p.OBJ_PROC_CAUSE_EXIT and status[8]==0
assert after>before
print('GRAPH-RACE-PASS',gid,before,after)
