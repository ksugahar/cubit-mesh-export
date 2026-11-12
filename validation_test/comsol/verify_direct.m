function verify_direct(root)
import com.comsol.model.util.*

before=sort(cell(ModelUtil.tags()));
assert(isempty(before), 'The dedicated COMSOL server must be empty');
tag='codex_cubit_direct_webcut_20261112';
assert(~ismember(tag,before));
out=struct('status','FAILED','pid',feature('getpid'),'before',{before});
try
    path=fullfile(root,'webcut-direct.mphtxt');
    source=read_mesh(path);
    mapping=jsondecode(fileread([path '.json']));
    m=ModelUtil.create(tag);
    guard=onCleanup(@()ModelUtil.remove(tag));
    c=m.component.create('comp1',true);
    c.geom.create('geom1',3).lengthUnit('m');
    mesh=c.mesh.create('mesh1');
    imp=mesh.create('imp1','Import');
    imp.set('filename',path);
    mesh.run;
    [stats,data]=mphmeshstats(m,'mesh1');
    out.mesh=stats;
    assert(stats.secondorderelements,'COMSOL did not retain quadratic elements');
    mesh.export().set('nativequadratic','on');
    roundtrip=fullfile(root,'comsol-roundtrip.mphtxt');
    mesh.export(roundtrip);
    native=read_mesh(roundtrip);
    [found,nodeMap]=ismember(native.points,source.points,'rows');
    assert(all(found) && numel(unique(nodeMap))==size(source.points,1),'quadratic coordinate map differs');
    native.tet=reshape(nodeMap(native.tet),size(native.tet));
    native.tri=reshape(nodeMap(native.tri),size(native.tri));
    compare_edges(source.tet,native.tet,[1 2;1 3;2 3;1 4;2 4;3 4]);
    compare_edges(source.tri,native.tri,[1 2;1 3;2 3]);
    out.quadratic_tets=size(native.tet,1);
    out.quadratic_triangles=size(native.tri,1);
    out.quadratic_nodes=numel(nodeMap);
    tetidx=find(ismember(string(data.types),["tet","tet2"]));
    triidx=find(ismember(string(data.types),["tri","tri2"]));
    out.domains=unique(data.elementity{tetidx});
    assert(numel(out.domains)==2);
    faceNodes=data.elem{triidx}+1;
    faces=data.elementity{triidx};
    x=reshape(data.vertex(1,faceNodes),size(faceNodes));
    names={'fixed_end','webcut_interface','loaded_end'};
    for i=1:3
        ids=unique(faces(all(abs(x-(i-2))<1e-12,1)));
        assert(isscalar(ids));
        sideset=mapping.sidesets(strcmp({mapping.sidesets.name},names{i}));
        q=source.tri(ismember(source.triEntity,sideset.file_indices),:);
        assert(size(q,1)==16);
        [ok,faceIndex]=ismember(sort(q(:,1:3),2),sort(native.tri(:,1:3),2),'rows');
        assert(all(ok) && numel(unique(native.triEntity(faceIndex)))==1);
        owners=mphgetadj(m,'geom1','domain','boundary',ids);
        assert(numel(owners)==1+(i==2));
        select=c.selection.create(names{i},'Explicit');
        select.geom('geom1',2);
        select.set(ids);
        readback=mphgetselection(select);
        assert(isequal(double(readback.entities(:)),double(ids(:))));
        out.selections(i)=struct('name',names{i},'entity',ids,'domains',owners,'faces',size(q,1));
        if i==2
            incidence=false(size(q,1),2);
            for j=1:size(q,1)
                hits=sum(ismember(native.tet(:,1:4),q(j,1:3)),2)==3;
                adjacent=native.tet(hits,:);
                assert(size(adjacent,1)==2 && all(sum(ismember(adjacent,q(j,:)),2)==6));
                assert(numel(unique(native.tetEntity(hits)))==2);
                incidence(j,:)=true;
            end
            out.shared_quadratic_faces=sum(all(incidence,2));
            out.shared_quadratic_nodes=numel(unique(q));
        end
    end
    out.status='TOPOLOGY_VERIFIED';
    write_json(fullfile(root,'topology.json'),out);
    mat=c.material.create('mat1','Common');
    mat.propertyGroup('def').set('youngsmodulus','200e9[Pa]');
    mat.propertyGroup('def').set('poissonsratio','0');
    mat.propertyGroup('def').set('density','7850[kg/m^3]');
    mat.propertyGroup('def').set('thermalconductivity','10[W/(m*K)]');
    mat.propertyGroup('def').set('heatcapacity','500[J/(kg*K)]');
    solid=c.physics.create('solid','SolidMechanics','geom1');
    fix=solid.create('fix1','Fixed',2);
    fix.selection.named('fixed_end');
    load=solid.create('load1','BoundaryLoad',2);
    load.selection.named('loaded_end');
    load.set('FperArea',{'2e5[Pa]','0','0'});
    ht=c.physics.create('ht','HeatTransfer','geom1');
    cold=ht.create('tleft','TemperatureBoundary',2);
    cold.selection.named('fixed_end');
    cold.set('T0','300[K]');
    hot=ht.create('tright','TemperatureBoundary',2);
    hot.selection.named('loaded_end');
    hot.set('T0','302[K]');
    study=m.study.create('std1');
    step=study.create('stat','Stationary');
    step.activate('ht',false);
    study.run;
    pts=[-0.75 -0.25 0.25 0.75;0 0 0 0;0 0 0 0];
    [u,v,w]=mphinterp(m,{'u','v','w'},'coord',pts);
    expected_u=1e-6*(pts(1,:)+1);
    assert(all(isfinite([u,v,w]),'all'));
    out.displacement_error_m=max(vecnorm([u-expected_u;v;w]));
    out.displacement_samples_m=[u;v;w];
    out.expected_displacement_m=expected_u;

    heat=m.study.create('heatonly');
    step=heat.create('stat','Stationary');
    step.activate('solid',false);
    step.activate('ht',true);
    existing=cell(m.sol.tags());
    heat.createAutoSequences('all');
    soltags=setdiff(cell(m.sol.tags()),existing);
    assert(numel(soltags)==1);
    solver=m.sol(soltags{1});
    stationary=solver.feature('s1');
    stationary.set('control','user');
    stationary.set('stol',1e-9);
    assert(str2double(char(stationary.getString('stol')))==1e-9);
    solver.runAll;
    dataset='';
    candidates=cell(m.result.dataset.tags());
    for i=1:numel(candidates)
        entry=m.result.dataset(candidates{i});
        if strcmp(char(entry.getType()),'Solution') && strcmp(char(entry.getString('solution')),soltags{1})
            dataset=candidates{i};
            break
        end
    end
    assert(~isempty(dataset));
    x=[-1 -0.75 -0.25 0.25 0.75 1];
    out.temperature_K=mphinterp(m,'T','coord',[x;zeros(2,numel(x))],'dataset',dataset,'unit','K','ext',0);
    out.expected_temperature_K=301+x;
    assert(all(isfinite(out.temperature_K)));
    out.temperature_error_K=max(abs(out.temperature_K-out.expected_temperature_K));
    mphsave(m,fullfile(root,'webcut-direct-verified.mph'));
    out.status='MEASURED';
catch err
    out.error=getReport(err,'extended','hyperlinks','off');
end
clear guard
out.after=sort(cell(ModelUtil.tags()));
out.prior_models_preserved=isequal(before,out.after);
write_json(fullfile(root,'comsol-result.json'),out);
assert(strcmp(out.status,'MEASURED') && out.prior_models_preserved, 'COMSOL validation failed; see result JSON');
end

function out=read_mesh(path)
lines=splitlines(string(fileread(path)));
lines=lines(strlength(strtrim(lines))>0);
h=find(contains(lines,'# Mesh vertex coordinates'),1);
n=sscanf(lines(h-1),'%d',1);
out.points=read_rows(lines,h+1,n,3,'%f');
for kind=["tet","tri"]
    h=find(contains(lines,kind+"2 # type name"),1);
    assert(~isempty(h),'missing quadratic element type');
    width=sscanf(lines(h+1),'%d',1);
    count=sscanf(lines(h+2),'%d',1);
    elem=find(strtrim(lines(h:end))=="# Elements",1)+h-1;
    out.(kind)=read_rows(lines,elem+1,count,width,'%d')+1;
    g=find(contains(lines(elem:end),'# Geometric entity indices'),1)+elem-1;
    out.(kind+"Entity")=read_rows(lines,g+1,count,1,'%d');
end
end

function rows=read_rows(lines,start,n,width,fmt)
rows=zeros(n,width);
j=start;
for k=1:n
    while strlength(strtrim(lines(j)))==0
        j=j+1;
    end
    rows(k,:)=sscanf(lines(j),fmt,width).';
    j=j+1;
end
end

function compare_edges(src,dst,edge)
assert(size(src,1)==size(dst,1));
n=size(src,2)-size(edge,1);
[ok,k]=ismember(sort(dst(:,1:n),2),sort(src(:,1:n),2),'rows');
assert(all(ok) && numel(unique(k))==size(src,1));
src=src(k,:);
for i=1:size(edge,1)
    a=min(dst(:,edge(i,:)),[],2);
    b=max(dst(:,edge(i,:)),[],2);
    same=false(size(a));
    for j=1:size(edge,1)
        same=same | (a==min(src(:,edge(j,:)),[],2) & b==max(src(:,edge(j,:)),[],2) & dst(:,n+i)==src(:,n+j));
    end
    assert(all(same),'quadratic edge-node correspondence differs');
end
end

function write_json(path,out)
fid=fopen(path,'w');
assert(fid>0);
guard=onCleanup(@()fclose(fid));
fprintf(fid,'%s\n',jsonencode(out));
end
