#!/usr/bin/env python3
"""Structural validator for FBX 7.4 binary files emitted by ExportFBXNative."""
import argparse
import collections
import math
import struct
import sys
import zlib

MAGIC = b"Kaydara FBX Binary  \x00\x1a\x00"
NULL_RECORD = b"\x00" * 13

class FBXError(Exception):
    pass

class Reader:
    def __init__(self, data):
        self.data = data

    def prop(self, pos, limit):
        d = self.data
        if pos >= limit:
            raise FBXError("property starts outside property list")
        typ = chr(d[pos]); pos += 1
        if typ == 'Y':
            val = struct.unpack_from('<h', d, pos)[0]; pos += 2
        elif typ == 'C':
            val = d[pos] != 0; pos += 1
        elif typ == 'I':
            val = struct.unpack_from('<i', d, pos)[0]; pos += 4
        elif typ == 'F':
            val = struct.unpack_from('<f', d, pos)[0]; pos += 4
        elif typ == 'D':
            val = struct.unpack_from('<d', d, pos)[0]; pos += 8
        elif typ == 'L':
            val = struct.unpack_from('<q', d, pos)[0]; pos += 8
        elif typ in ('S', 'R'):
            n = struct.unpack_from('<I', d, pos)[0]; pos += 4
            val = d[pos:pos+n]; pos += n
            if len(val) != n:
                raise FBXError("truncated string/raw property")
            if typ == 'S':
                val = val.decode('utf-8', 'replace')
        elif typ in ('f', 'd', 'i', 'l', 'b', 'c'):
            count, encoding, compressed_len = struct.unpack_from('<III', d, pos); pos += 12
            item_size = {'f':4, 'd':8, 'i':4, 'l':8, 'b':1, 'c':1}[typ]
            raw = d[pos:pos+compressed_len]; pos += compressed_len
            if len(raw) != compressed_len:
                raise FBXError("truncated array property")
            if encoding == 0:
                payload = raw
            elif encoding == 1:
                payload = zlib.decompress(raw)
            else:
                raise FBXError("unknown array encoding %d" % encoding)
            if len(payload) != count * item_size:
                raise FBXError("array byte count mismatch")
            fmt = {'f':'f','d':'d','i':'i','l':'q','b':'B','c':'B'}[typ]
            val = list(struct.unpack('<' + fmt * count, payload)) if count else []
        else:
            raise FBXError("unknown property type %r at 0x%x" % (typ, pos-1))
        if pos > limit:
            raise FBXError("property overran property list")
        return (typ, val), pos

    def node(self, pos, parent_limit):
        d = self.data
        if pos + 13 > parent_limit:
            raise FBXError("short node header at 0x%x" % pos)
        end, prop_count, prop_len, name_len = struct.unpack_from('<IIIB', d, pos)
        if (end, prop_count, prop_len, name_len) == (0,0,0,0):
            return None, pos + 13
        if end <= pos or end > parent_limit:
            raise FBXError("invalid end offset at 0x%x: %d (parent limit %d)" % (pos, end, parent_limit))
        name_start = pos + 13
        prop_start = name_start + name_len
        if prop_start > end:
            raise FBXError("name overruns node")
        name = d[name_start:prop_start].decode('utf-8', 'replace')
        q = prop_start
        props = []
        for _ in range(prop_count):
            p, q = self.prop(q, end)
            props.append(p)
        actual_prop_len = q - prop_start
        if actual_prop_len != prop_len:
            raise FBXError("%s property-list length mismatch: header=%d actual=%d" % (name, prop_len, actual_prop_len))
        children = []
        if q < end:
            while q + 13 <= end:
                if d[q:q+13] == NULL_RECORD:
                    q += 13
                    break
                child, q2 = self.node(q, end)
                if child is None:
                    q = q2
                    break
                children.append(child)
                q = q2
            if q != end:
                raise FBXError("%s child list does not end at endOffset (%d != %d)" % (name, q, end))
        elif q != end:
            raise FBXError("%s property list ends beyond node" % name)
        return {'name':name, 'props':props, 'children':children, 'start':pos, 'end':end}, end


def children(node, name):
    return [x for x in node['children'] if x['name'] == name]

def first(node, name):
    xs = children(node, name)
    return xs[0] if xs else None

def pval(node, idx=0, default=None):
    try: return node['props'][idx][1]
    except (IndexError, TypeError): return default

def collect(node, name, out=None):
    if out is None: out=[]
    if node['name'] == name: out.append(node)
    for c in node['children']: collect(c, name, out)
    return out

def validate(path):
    data = open(path, 'rb').read()
    errors, warnings = [], []
    if len(data) < 27 + 13 + 160:
        raise FBXError("file too small")
    if data[:23] != MAGIC:
        raise FBXError("invalid FBX binary signature")
    version = struct.unpack_from('<I', data, 23)[0]
    if version != 7400:
        warnings.append("FBX version is %d, expected 7400" % version)
    footer_start = len(data) - 160
    if footer_start < 27:
        raise FBXError("invalid footer position")
    r = Reader(data)
    roots=[]; pos=27
    while pos + 13 <= footer_start:
        if data[pos:pos+13] == NULL_RECORD:
            pos += 13
            break
        n, pos = r.node(pos, footer_start)
        if n is None: break
        roots.append(n)
    if pos > footer_start:
        raise FBXError("root list overlaps footer")
    if any(data[pos:footer_start]):
        warnings.append("non-zero bytes between root terminator and footer")
    if (footer_start + 20) % 16:
        warnings.append("footer version word is not 16-byte aligned")

    root_by_name = {x['name']:x for x in roots}
    for required in ('FBXHeaderExtension','GlobalSettings','Documents','Definitions','Objects','Connections','Takes'):
        if required not in root_by_name:
            errors.append("missing top-level node %s" % required)
    creator = first(root_by_name.get('FBXHeaderExtension', {'children':[]}), 'Creator')
    if creator: print("Creator:", pval(creator))

    objects = root_by_name.get('Objects')
    ids = {}
    type_counts = collections.Counter()
    if objects:
        for obj in objects['children']:
            typ = obj['name']
            type_counts[typ] += 1
            oid = pval(obj, 0)
            if not isinstance(oid, int):
                errors.append("%s has no integer object ID" % typ)
            elif oid in ids:
                errors.append("duplicate object ID %s" % oid)
            else:
                ids[oid] = obj

    definitions = root_by_name.get('Definitions')
    if definitions:
        declared_total = pval(first(definitions,'Count'))
        actual_total = 1 + sum(type_counts.values())
        if declared_total != actual_total:
            errors.append("Definitions Count mismatch: declared %s actual %s" % (declared_total, actual_total))
        for ot in children(definitions,'ObjectType'):
            typ = pval(ot,0)
            count = pval(first(ot,'Count'))
            if typ == 'GlobalSettings': actual = 1
            else: actual = type_counts.get(typ,0)
            if count != actual:
                errors.append("Definitions %s Count mismatch: declared %s actual %s" % (typ,count,actual))
            for template in children(ot,'PropertyTemplate'):
                props70 = first(template,'Properties70')
                if props70 is None:
                    errors.append("%s template has no Properties70" % typ)

    doc = first(root_by_name.get('Documents', {'children':[]}), 'Document')
    root_node = first(doc,'RootNode') if doc else None
    if root_node and pval(root_node) != 0:
        errors.append("Documents/Document/RootNode is not 0")
    if not root_node:
        errors.append("Documents/Document/RootNode missing")

    # Object connections must reference existing objects or the scene root (0).
    conns = []
    conn_node = root_by_name.get('Connections')
    if conn_node:
        for c in children(conn_node,'C'):
            if len(c['props']) < 3:
                errors.append("malformed Connections/C node")
                continue
            kind, src, dst = [x[1] for x in c['props'][:3]]
            conns.append((kind,src,dst,c))
            if src not in ids:
                errors.append("connection source ID %s does not exist" % src)
            if dst != 0 and dst not in ids:
                errors.append("connection destination ID %s does not exist" % dst)

    model_ids = {oid for oid,o in ids.items() if o['name']=='Model'}
    geometry_ids = {oid for oid,o in ids.items() if o['name']=='Geometry'}
    material_ids = {oid for oid,o in ids.items() if o['name']=='Material'}
    attr_ids = {oid for oid,o in ids.items() if o['name']=='NodeAttribute'}
    model_geom = collections.defaultdict(list)
    model_mats = collections.defaultdict(list)
    model_attrs = collections.defaultdict(list)
    for kind,src,dst,c in conns:
        if kind != 'OO': continue
        if src in geometry_ids and dst in model_ids: model_geom[dst].append(src)
        if src in material_ids and dst in model_ids: model_mats[dst].append(src)
        if src in attr_ids and dst in model_ids: model_attrs[dst].append(src)
    for mid in model_ids:
        m=model_mats[mid]
        for geom in model_geom[mid]:
            g=ids[geom]
            pvi=first(g,'PolygonVertexIndex'); verts=first(g,'Vertices'); normals=first(first(g,'LayerElementNormal') or {'children':[]},'Normals')
            if not pvi or not verts:
                errors.append("Geometry %s missing Vertices or PolygonVertexIndex" % geom); continue
            vi=pval(pvi,0,[]); vc=pval(verts,0,[])
            if len(vc)%3: errors.append("Geometry %s vertex array length is not divisible by 3" % geom)
            vertex_count=len(vc)//3
            polygon_count=0; corner_count=len(vi)
            for x in vi:
                idx=(~x) if x<0 else x
                if idx<0 or idx>=vertex_count: errors.append("Geometry %s has out-of-range vertex index %s" % (geom,idx));break
                if x<0: polygon_count+=1
            if vi and vi[-1]>=0: errors.append("Geometry %s final polygon is not closed" % geom)
            if normals:
                normal_values=pval(normals,0,[])
                normal_ref=pval(first(first(g,'LayerElementNormal') or {'children':[]},'ReferenceInformationType'))
                normal_indices=first(first(g,'LayerElementNormal') or {'children':[]},'NormalsIndex')
                if len(normal_values)%3:
                    errors.append("Geometry %s normal array length is not divisible by 3" % geom)
                elif normal_ref=='Direct' and len(normal_values)!=corner_count*3:
                    errors.append("Geometry %s direct normal count does not match polygon corners" % geom)
                elif normal_ref=='IndexToDirect':
                    ni=pval(normal_indices,0,[]) if normal_indices else []
                    if len(ni)!=corner_count or any(i<0 or i>=len(normal_values)//3 for i in ni):
                        errors.append("Geometry %s normal index array invalid" % geom)
            for uvnode in children(g,'LayerElementUV'):
                uv=first(uvnode,'UV'); uvi=first(uvnode,'UVIndex')
                uva=pval(uv,0,[]) if uv else []; ia=pval(uvi,0,[]) if uvi else []
                uv_ref=pval(first(uvnode,'ReferenceInformationType'))
                if len(uva)%2:
                    errors.append("Geometry %s UV array length is not divisible by 2" % geom)
                elif uv_ref=='Direct' and len(uva)!=corner_count*2:
                    errors.append("Geometry %s direct UV count does not match polygon corners" % geom)
                elif uv_ref=='IndexToDirect' and (len(ia)!=corner_count or any(i<0 or i>=len(uva)//2 for i in ia)):
                    errors.append("Geometry %s UV index array invalid" % geom)
            matnode=first(g,'LayerElementMaterial')
            if matnode:
                ma=first(matnode,'Materials'); marr=pval(ma,0,[]) if ma else []
                mapping=pval(first(matnode,'MappingInformationType'))
                expected={'ByPolygon':polygon_count,'ByPolygonVertex':corner_count,'AllSame':1}.get(mapping)
                if expected is not None and len(marr)!=expected:
                    errors.append("Geometry %s material mapping count mismatch (%s: %d != %d)" % (geom,mapping,len(marr),expected))
                if any(i<0 or i>=len(m) for i in marr):
                    errors.append("Geometry %s material index exceeds connected model material slots" % geom)
            edges=first(g,'Edges')
            if edges and any(i<0 or i>=corner_count for i in pval(edges,0,[])):
                errors.append("Geometry %s Edges index out of range" % geom)
            smnode=first(g,'LayerElementSmoothing')
            if smnode:
                sm=pval(first(smnode,'Smoothing'),0,[])
                smap=pval(first(smnode,'MappingInformationType'))
                sref=pval(first(smnode,'ReferenceInformationType'))
                if isinstance(smap,bytes): smap=smap.decode('utf-8','replace')
                if isinstance(sref,bytes): sref=sref.decode('utf-8','replace')
                if sref!='Direct' or smap not in ('ByEdge','ByPolygon'):
                    errors.append("Geometry %s smoothing must be ByEdge or ByPolygon/Direct (got %s/%s)" % (geom,smap,sref))
                elif smap=='ByPolygon':
                    # 3ds Max smoothing groups: one 32-bit group bitmask per polygon.
                    if len(sm)!=polygon_count:
                        errors.append("Geometry %s smoothing count %d does not match polygon count %d" % (geom,len(sm),polygon_count))
                else:
                    # Crease-only path: 0/1 hard-edge flag per edge.
                    if edges is None or len(sm)!=len(pval(edges,0,[])):
                        errors.append("Geometry %s smoothing count %d does not match Edges count" % (geom,len(sm)))
                    elif any(v not in (0,1) for v in sm):
                        errors.append("Geometry %s smoothing values must be 0 or 1" % geom)
            layer=first(g,'Layer')
            if not layer or pval(layer,0) != 0:
                errors.append("Geometry %s Layer index missing or not zero" % geom)
        # Mesh models need geometry; Null models need a NodeAttribute.
        model=ids[mid]
        mtype=pval(model,2)
        if mtype=='Mesh' and not model_geom[mid]:
            errors.append("Mesh Model %s has no Geometry connection" % mid)
        if mtype=='Null' and not model_attrs[mid]:
            warnings.append("Null Model %s has no NodeAttribute connection" % mid)

    # Texture / Video node checks.
    texture_ids = {oid for oid,o in ids.items() if o['name']=='Texture'}
    video_ids = {oid for oid,o in ids.items() if o['name']=='Video'}
    video_media = set()
    for vid in video_ids:
        v = ids[vid]
        if first(v,'Filename') is None:
            errors.append("Video %s missing Filename" % vid)
        vname = pval(v,1,b'')
        if isinstance(vname,bytes): vname = vname.decode('utf-8','replace')
        video_media.add("Video::"+str(vname).split('\x00')[0])
    tex_to_mat = {}
    tex_has_video = set()
    for kind,src,dst,c in conns:
        if kind=='OP' and src in texture_ids and dst in material_ids:
            prop = c['props'][3][1] if len(c['props'])>3 else None
            if isinstance(prop,bytes): prop = prop.decode('utf-8','replace')
            if prop not in ('DiffuseColor','TransparentColor','Bump'):
                errors.append("Texture %s connected to Material with unexpected property %r" % (src,prop))
            tex_to_mat[src]=dst
        if kind=='OO' and src in video_ids and dst in texture_ids:
            tex_has_video.add(dst)
    for tid in texture_ids:
        if tid not in tex_to_mat:
            errors.append("Texture %s has no OP connection to a Material" % tid)
        if tid not in tex_has_video:
            errors.append("Texture %s has no Video connection" % tid)
        media = first(ids[tid],'Media')
        if media is not None:
            mv = pval(media,0,b'')
            if isinstance(mv,bytes): mv = mv.decode('utf-8','replace')
            if mv and mv not in video_media:
                warnings.append("Texture %s Media %r does not match any Video node name" % (tid,mv))

    print("FBX version:",version)
    print("File size:",len(data))
    print("Object counts:",dict(type_counts))
    print("Connections:",len(conns))
    if errors:
        print("\nERRORS (%d):" % len(errors))
        for x in errors: print(" -",x)
    if warnings:
        print("\nWARNINGS (%d):" % len(warnings))
        for x in warnings: print(" -",x)
    print("\nRESULT:","PASS" if not errors else "FAIL")
    return 0 if not errors else 2

if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('fbx',help='FBX Binary file to validate')
    args=ap.parse_args()
    try: sys.exit(validate(args.fbx))
    except (FBXError,struct.error,zlib.error) as e:
        print("FATAL:",e,file=sys.stderr);sys.exit(3)
