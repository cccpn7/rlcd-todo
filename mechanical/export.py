"""Export the accepted enclosure and audit its meshes; Python standard library only."""
from pathlib import Path
from collections import defaultdict
import json
import os
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parent / 'local' / 'mechanical' / 'export'
NAMES = ['shell','cover','bracket']
SCAD = os.environ.get('OPENSCAD') or shutil.which('openscad')
if not SCAD:
    raise SystemExit('Set OPENSCAD to the OpenSCAD executable, or add openscad to PATH.')
STL = ROOT / 'stl'
CHECK = ROOT / 'validation'
STL.mkdir(parents=True, exist_ok=True)
CHECK.mkdir(exist_ok=True)
reports, meshes = {}, {}
expected = {'shell':(81,156,17.2), 'cover':(81,156,13.2), 'bracket':(56.606742803667316,21.60025961039373,56)}

for name in NAMES:
    result = subprocess.run([SCAD, '--hardwarnings', '--export-format', 'asciistl',
        '-D', f'part="{name}"', '-o', str(STL / f'{name}.stl'), str(SOURCE/'enclosure.scad')],
        capture_output=True, text=True)
    (CHECK / f'{name}-export.log').write_text(result.stdout + result.stderr)
    if result.returncode or 'WARNING:' in result.stderr or 'ERROR:' in result.stderr:
        raise RuntimeError(f'{name}: OpenSCAD export failed; inspect saved log')
    vertices, index, faces, tri = [], {}, [], []
    for line in (STL / f'{name}.stl').read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == 'vertex':
            v = tuple(round(float(x), 6) for x in fields[1:])
            if v not in index:
                index[v] = len(vertices)
                vertices.append(v)
            tri.append(index[v])
            if len(tri) == 3:
                faces.append(tuple(tri))
                tri = []
    assert vertices and faces and not tri, name
    edges, directions = defaultdict(list), defaultdict(int)
    parent = list(range(len(vertices)))
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    volume = 0
    for n, (a,b,c) in enumerate(faces):
        assert len({a,b,c}) == 3, f'{name}: degenerate triangle'
        va,vb,vc = vertices[a],vertices[b],vertices[c]
        ab = [vb[i]-va[i] for i in range(3)]
        ac = [vc[i]-va[i] for i in range(3)]
        cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
        assert sum(x*x for x in cross) > 1e-18, f'{name}: zero area face'
        volume += (va[0]*(vb[1]*vc[2]-vb[2]*vc[1]) + va[1]*(vb[2]*vc[0]-vb[0]*vc[2]) + va[2]*(vb[0]*vc[1]-vb[1]*vc[0]))/6
        for p,q in [(a,b),(b,c),(c,a)]:
            key = tuple(sorted((p,q)))
            edges[key].append(n)
            directions[key] += 1 if p < q else -1
            parent[find(p)] = find(q)
    bounds = [(min(v[k] for v in vertices), max(v[k] for v in vertices)) for k in range(3)]
    dims = [b-a for a,b in bounds]
    assert all(len(f) == 2 for f in edges.values()), f'{name}: open/nonmanifold edges'
    assert all(d == 0 for d in directions.values()), f'{name}: inconsistent face winding'
    assert len({find(i) for i in range(len(vertices))}) == 1, f'{name}: disconnected shells'
    assert volume > 0, f'{name}: inverted/empty solid'
    assert abs(bounds[2][0]) < 1e-5, f'{name}: not flat on print bed'
    assert all(abs(a-b) < .02 for a,b in zip(dims, expected[name])), f'{name}: unexpected bounds {dims}'
    reports[name] = dict(triangles=len(faces), closed_manifold=True, consistent_winding=True,
        connected_components=1, bounds_mm=bounds, size_mm=dims, volume_mm3=round(volume,2))
    meshes[name] = vertices, faces
    print(name, 'PASS', [round(x,2) for x in dims], flush=True)

# Standard, machine-neutral 3MF: 3 resources, 3 separate build items.
NS = 'http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
ET.register_namespace('', NS)
model = ET.Element(f'{{{NS}}}model', {'unit':'millimeter', 'xml:lang':'en-US'})
resources = ET.SubElement(model, f'{{{NS}}}resources')
for oid,name in enumerate(NAMES, 1):
    verts,faces = meshes[name]
    obj = ET.SubElement(resources, f'{{{NS}}}object', {'id':str(oid),'type':'model','name':name})
    mesh = ET.SubElement(obj, f'{{{NS}}}mesh')
    vs = ET.SubElement(mesh, f'{{{NS}}}vertices')
    ts = ET.SubElement(mesh, f'{{{NS}}}triangles')
    for x,y,z in verts:
        ET.SubElement(vs, f'{{{NS}}}vertex', {'x':str(x),'y':str(y),'z':str(z)})
    for a,b,c in faces:
        ET.SubElement(ts, f'{{{NS}}}triangle', {'v1':str(a),'v2':str(b),'v3':str(c)})
build = ET.SubElement(model, f'{{{NS}}}build')
placements = [('shell',6,10),('cover',99,10),('bracket',99,184)]
assert len(placements)==3
boxes=[(x,y,x+reports[n]['size_mm'][0],y+reports[n]['size_mm'][1]) for n,x,y in placements]
for i,(x,y,x2,y2) in enumerate(boxes):
    assert x>=5 and y>=5 and x2<=251 and y2<=251
    for ox,oy,ox2,oy2 in boxes[:i]: assert x>=ox2+1 or ox>=x2+1 or y>=oy2+1 or oy>=y2+1
(CHECK/'plate-layout.json').write_text(json.dumps(placements,indent=2))
with (ROOT/'enclosure-v5-plate.stl').open('w') as out:
    out.write('solid three_parts\n')
    for name,x,y in placements:
        verts,faces=meshes[name]
        for face in faces:
            out.write('  facet normal 0 0 0\n    outer loop\n')
            for idx in face:
                vx,vy,vz=verts[idx]
                out.write(f'      vertex {vx+x:.6f} {vy+y:.6f} {vz:.6f}\n')
            out.write('    endloop\n  endfacet\n')
    out.write('endsolid three_parts\n')
for name,x,y in placements:
    ET.SubElement(build, f'{{{NS}}}item', {'objectid':str(NAMES.index(name)+1),
        'transform':f'1 0 0 0 1 0 0 0 1 {x} {y} 0'})
with zipfile.ZipFile(ROOT/'enclosure-v5-plate.3mf','w',zipfile.ZIP_DEFLATED) as z:
    z.writestr('[Content_Types].xml', '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    z.writestr('_rels/.rels', '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    z.writestr('3D/3dmodel.model', ET.tostring(model, encoding='utf-8', xml_declaration=True))
(CHECK/'mesh-check.json').write_text(json.dumps(reports,indent=2))
print('3MF ready: 3 parts, one P2S plate; cover back rests flat on bed; bracket stands on its side.')
