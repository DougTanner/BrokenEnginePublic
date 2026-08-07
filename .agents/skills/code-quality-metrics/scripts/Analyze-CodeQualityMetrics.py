#!/usr/bin/env python3
"""Copy-only deterministic adapter for the provisioned scb-check analyzer."""
from __future__ import annotations

import argparse, contextlib, hashlib, json, math, os, re, stat, subprocess, sys, tempfile
from collections import defaultdict
from pathlib import Path, PurePosixPath
from typing import Any, Iterable
from normalize_code_quality_metrics_cpp import normalize_cpp_bytes

SCHEMA="broken-engine-code-quality-metrics/v2"; TARGETS_SCHEMA="broken-engine-code-quality-targets/v1"; PROFILE="BrokenEngineExtended"
EXCLUDED={"ThirdParty",".agents",".claude","Temp"}; REASONS=("baseline-inapplicable","context-change","current-inapplicable","membership-change","parse-status-change")
RENAME_SIMILARITY=50
MAX_ANALYZER_SOURCE_ENTRIES=1024; MAX_ANALYZER_SOURCE_BYTES=1024*1024

class MetricsError(RuntimeError): pass
class TargetMetricFailure(MetricsError):
 def __init__(self,code:str,message:str,failures:list[dict[str,Any]]):
  super().__init__(message);self.code=code;self.message=message;self.failures=failures
def fail(s:str)->None: raise MetricsError(s)
def digest(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def run(root:Path,*a:str)->bytes:
 r=subprocess.run(["git","-C",str(root),*a],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 if r.returncode: fail(r.stderr.decode("utf-8","replace").strip() or "git failed")
 return r.stdout
def canon(s:str)->str:
 if not isinstance(s,str) or not s or "\\" in s or s.startswith("/"): fail("path must be canonical relative POSIX")
 p=PurePosixPath(s)
 if any(x in {"",".",".."} for x in p.parts): fail("path must be canonical relative POSIX")
 return "/".join(p.parts)
def pure_glsl_header(p:str)->bool:
 if Path(p).suffix.lower()!=".h":return False
 parts=PurePosixPath(p).parts
 return any(parts[index:index+2]==("Data","Shaders") for index in range(len(parts)-1)) and parts[-1] not in {"ShaderLayouts.h","ShaderLayoutsBase.h"}
def supported(p:str)->bool:
 return p.split("/",1)[0] not in EXCLUDED and Path(p).suffix.lower() in {".c++",".cc",".cpp",".cxx",".h",".hh",".hpp",".hxx"} and not pure_glsl_header(p)
def check_components(root:Path,p:Path,require_file:bool)->None:
 try: p.relative_to(root)
 except ValueError: fail("path escapes repository")
 current=root
 for part in p.relative_to(root).parts:
  current=current/part
  if current.exists() and (current.is_symlink() or getattr(current.lstat(),"st_file_attributes",0)&0x400): fail("reparse path is not allowed")
 if require_file and (not p.is_file()): fail("path is not an ordinary file")
def validate_analyzer_source(root:Path,raw:Any)->Path:
 if not isinstance(raw,str) or not raw or not Path(raw).is_absolute(): fail("analyzer source must be an absolute path")
 source=Path(os.path.abspath(raw))
 if raw.casefold()!=str(source).casefold(): fail("analyzer source must be lexically canonical")
 cache=root/"Temp"/"CodeQualityMetrics"
 try: relative=source.relative_to(cache)
 except ValueError: fail("analyzer source must be beneath Temp/CodeQualityMetrics")
 if len(relative.parts)!=3 or relative.parts[1:]!=("source","src") or not re.fullmatch(r"m-[0-9a-f]{8}",relative.parts[0]): fail("analyzer source has an invalid materialization path")
 check_components(root,source,False)
 if not source.is_dir(): fail("analyzer source must be an ordinary directory")
 entries=0; total_bytes=0; pending=[source]
 while pending:
  with os.scandir(pending.pop()) as scan:
   for entry in scan:
    entries+=1
    if entries>MAX_ANALYZER_SOURCE_ENTRIES: fail("analyzer source exceeds the trusted entry bound")
    info=entry.stat(follow_symlinks=False)
    if entry.is_symlink() or getattr(info,"st_file_attributes",0)&0x400: fail("analyzer source contains a reparse path")
    if stat.S_ISDIR(info.st_mode): pending.append(Path(entry.path))
    elif stat.S_ISREG(info.st_mode):
     total_bytes+=info.st_size
     if total_bytes>MAX_ANALYZER_SOURCE_BYTES: fail("analyzer source exceeds the trusted byte bound")
    else: fail("analyzer source contains a nonordinary entry")
 if entries==0: fail("analyzer source is empty")
 return source
def ident(path:str,mode:str,data:bytes)->dict[str,str]: return {"path":path,"mode":mode,"sha256":digest(data)}
def module_name(path:str)->str:
 name=PurePosixPath(path).name
 return name.removesuffix("".join(PurePosixPath(path).suffixes)) or PurePosixPath(path).stem
def identity_owner(path:str,owner:str|None)->str|None:
 if owner is None:return None
 prefix=module_name(path)+"."
 return owner[len(prefix):] if owner.startswith(prefix) else owner
def rename_pair(b:bytes,c:bytes,capture_root:Path)->bool:
 # Git's fixed similarity threshold is the trust boundary for a cross-path pair, including untracked current content.
 with tempfile.TemporaryDirectory(prefix="rename-",dir=capture_root) as t:
  before=Path(t)/"before";after=Path(t)/"after";before.mkdir();after.mkdir();(before/"old").write_bytes(b);(after/"new").write_bytes(c)
  r=subprocess.run(["git","-c","core.autocrlf=false","-c","diff.renames=true","diff","--no-index",f"--find-renames={RENAME_SIMILARITY}%","--name-status","-z","--no-ext-diff","--",str(before),str(after)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if r.returncode not in {0,1}:fail(r.stderr.decode("utf-8","replace").strip() or "git rename detection failed")
  return r.stdout.startswith(b"R")
def current(root:Path)->dict[str,tuple[dict[str,str],bytes]]:
 modes={}
 for x in run(root,"ls-files","--stage","-z").split(b"\0"):
  if x:
   m,n=x.split(b"\t",1); mode,_,stage=m.decode().split()
   if stage=="0":modes[n.decode("utf-8","surrogateescape")]=mode
 names=set()
 for args in (("ls-files","-z","--cached"),("ls-files","-z","--others","--exclude-standard")):
  names|={x.decode("utf-8","surrogateescape") for x in run(root,*args).split(b"\0") if x}
 out={}; folded=set()
 for raw in sorted(names):
  p=canon(raw); low=p.casefold()
  if low in folded: fail("case-insensitive path collision")
  folded.add(low)
  if not supported(p):continue
  f=root.joinpath(*p.split("/"))
  if not f.exists(): continue # tracked deletion is absent current state
  if f.is_symlink() or getattr(f.lstat(),"st_file_attributes",0)&0x400: continue # a nonordinary side is not corpus content
  check_components(root,f,True); b=f.read_bytes(); out[p]=(ident(p,modes.get(p,"100644"),b),b)
 return out
def baseline(root:Path,sha:str)->dict[str,tuple[dict[str,str],bytes]]:
 if len(sha)!=40 or any(c not in "0123456789abcdef" for c in sha):fail("Baseline must be full lowercase SHA")
 run(root,"cat-file","-e",sha+"^{commit}"); out={}; folded=set()
 for x in run(root,"ls-tree","-r","-z",sha).split(b"\0"):
  if not x:continue
  m,n=x.split(b"\t",1);mode,kind,_=m.decode().split();p=canon(n.decode("utf-8","surrogateescape"));low=p.casefold()
  if low in folded:fail("case-insensitive baseline collision")
  folded.add(low)
  if kind=="blob" and mode in {"100644","100755"} and supported(p):
   b=run(root,"show",f"{sha}:{p}");out[p]=(ident(p,mode,b),b)
 return out
def metric(n:float,d:float,app:bool=True)->dict[str,Any]:return {"applicable":bool(app and d!=0),"value":n/d if app and d else None,"numerator":n,"denominator":d}
def metrics(fs:Iterable[dict[str,Any]], lines:Iterable[tuple[str,int]], sloc:dict[str,set[int]])->dict[str,Any]:
 fs=list(fs); total=sum(len(x) for x in sloc.values()); mass=sum(x["mass"] for x in fs); high=sum(x["mass"] for x in fs if x["cc"]>10); union=defaultdict(set)
 for p,l in lines:
  if l in sloc.get(p,set()):union[p].add(l)
 return {"structuralErosion":metric(high,mass),"verbosity":metric(sum(map(len,union.values())),total)}
def area(p:str)->str:
 x=p.split("/")[:-1]
 if x[:2]==["Engine","Source"]:return "/".join(x[:3]) if len(x)>2 else "Engine/Source"
 if len(x)>=3 and x[0]=="Projects" and x[2]=="Source":return "/".join(x[:4]) if len(x)>3 else "/".join(x[:3])
 if x and x[0] in {"Common","Tools"}:return "/".join(x[:2]) if len(x)>1 else x[0]
 return x[0] if x else "[root]"
def aggregate(fs:list[dict[str,Any]])->dict[str,Any]:return {n:metric(sum(x["metrics"][n]["numerator"] for x in fs),sum(x["metrics"][n]["denominator"] for x in fs)) for n in ("structuralErosion","verbosity")}
def area_rows(fs:list[dict[str,Any]])->list[dict[str,Any]]:
 grouped=defaultdict(list)
 for x in fs:grouped[x["area"]].append(x)
 return [{"area":a,"metrics":aggregate(grouped[a])} for a in sorted(grouped)]
def outlier_buckets(files:list[dict[str,Any]],areas:list[dict[str,Any]],corpus_metrics:dict[str,Any])->list[dict[str,Any]]:
 out=[]
 for kind,rows,key in (("file",files,"path"),("area",areas,"area")):
  for name in ("structuralErosion","verbosity"):
   values=sorted(({"kind":kind,"metric":name,"key":x[key],"delta":x["metrics"][name]["value"]-corpus_metrics[name]["value"]} for x in rows if x["metrics"][name]["applicable"] and corpus_metrics[name]["applicable"] and x["metrics"][name]["value"]>corpus_metrics[name]["value"]),key=lambda x:(-x["delta"],x["key"]))
   out.append({"kind":kind,"metric":name,"items":values[:10],"totalCount":len(values),"truncatedCount":max(0,len(values)-10)})
 return out
def nodes(node:Any,kind:str)->Iterable[Any]:
 if node.type==kind:yield node
 for child in node.children:yield from nodes(child,kind)
def normalized_tree_problem(parsed:Any,source:bytes)->dict[str,int|str]|None:
 candidates=[];pending=[parsed.native_tree.root_node]
 while pending:
  node=pending.pop();pending.extend(node.children)
  if node.type=="ERROR":candidates.append((node.start_byte,0,"normalized-tree-error"))
  elif node.is_missing:candidates.append((node.start_byte,1,"normalized-tree-missing-node"))
 if not candidates:return None
 offset,_,code=min(candidates);offset=min(max(offset,0),len(source));line=source.count(b"\n",0,offset)+1;column=offset-(source.rfind(b"\n",0,offset)+1)
 return {"stage":"dispatch-parse","code":code,"line":line,"column":column}
def declarator_name(node:Any)->Any:
 if node.type in {"identifier","field_identifier"}:return node
 child=node.child_by_field_name("declarator")
 if child is not None:return declarator_name(child)
 for child in reversed(node.children):
  if child.type!="parameter_list":
   found=declarator_name(child)
   if found is not None:return found
 return None
def typed_signatures(parsed:Any)->dict[tuple[int,str],str|None]:
 source=parsed.source.encode("utf-8");out={}
 for definition in nodes(parsed.native_tree.root_node,"function_definition"):
  declarator=definition.child_by_field_name("declarator")
  if declarator is None:continue
  name=declarator_name(declarator)
  parameters=declarator.child_by_field_name("parameters")
  if name is None or parameters is None:continue
  key=(definition.start_point.row+1,source[name.start_byte:name.end_byte].decode("utf-8","replace"));masked=[];valid=True
  for parameter in parameters.children:
   if "parameter_declaration" not in parameter.type:continue
   parameter_declarator=parameter.child_by_field_name("declarator")
   if parameter_declarator is not None:
    parameter_name=declarator_name(parameter_declarator)
    if parameter_name is None:valid=False;break
    masked.append((parameter_name.start_byte,parameter_name.end_byte))
   default=parameter.child_by_field_name("default_value")
   if default is not None:
    equals=next((child for child in parameter.children if child.type=="="),None)
    if equals is None:valid=False;break
    masked.append((equals.start_byte,parameter.end_byte))
  body=definition.child_by_field_name("body")
  if body is None:valid=False
  if valid:
   tokens=[]
   def leaves(node:Any)->Iterable[Any]:
    if not node.children:yield node
    else:
     for child in node.children:yield from leaves(child)
   for leaf in leaves(definition):
    if leaf.type=="comment" or (body is not None and leaf.start_byte>=body.start_byte) or any(start<=leaf.start_byte<end for start,end in masked):continue
    tokens.append(source[leaf.start_byte:leaf.end_byte].decode("utf-8","replace"))
   value=" ".join(tokens) if tokens else None
  else:value=None
  if key in out:out[key]=None
  else:out[key]=value
 return out
def capture(entries:dict[str,tuple[dict[str,str],bytes]],capture_root:Path,analyzer_source:Path)->dict[str,Any]:
 empty={"structuralErosion":metric(0,0,False),"verbosity":metric(0,0,False)}
 if not entries:return {"corpusManifest":[],"corpusCounts":{"supported":0,"parsed":0,"omitted":0},"skips":[],"corpusMetrics":empty,"files":[],"areas":[],"outliers":[],"cloneGroups":[],"highComplexityFunctions":[],"_functions":[],"_parsed":set(),"_instances":[],"_failures":[]}
 sys.path.insert(0,str(analyzer_source))
 try:
  import scb_check.pipeline as pipe
  from scb_check.tree_walking.dispatch import DEFAULT_EXTENSION_LANGUAGES,Language,dispatch_parse
 except Exception as e:fail(f"analyzer import failed: {e}")
 original=pipe.dispatch_parse_source_file; mapping=dict(DEFAULT_EXTENSION_LANGUAGES); mapping[".h"]=(Language.CPP,)
 def parse(path:Path,source:str):return dispatch_parse(path,source,extension_languages=mapping)
 if pipe.dispatch_parse_source_file is not original:fail("unexpected upstream parser adapter")
 pipe.dispatch_parse_source_file=parse
 try:
  with tempfile.TemporaryDirectory(prefix="capture-",dir=capture_root) as t:
   cr=Path(t); pm={}; files=[]; sloc={}; signatures={};failures=[]
   for rel in sorted(entries):
    _,b=entries[rel]
    analysis_bytes=normalize_cpp_bytes(b)
    f=cr.joinpath(*rel.split("/"));f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(analysis_bytes);f=f.resolve();pm[f]=rel
    try:
     parsed=parse(f,analysis_bytes.decode("utf-8","replace"))
    except Exception:
     failures.append({"path":rel,"stage":"dispatch-parse","code":"dispatch-parse-failure","line":1,"column":0});continue
    else:
     try:problem=normalized_tree_problem(parsed,analysis_bytes)
     except Exception:
      failures.append({"path":rel,"stage":"dispatch-parse","code":"normalized-tree-unavailable","line":1,"column":0});continue
     if problem is not None:
      failures.append({"path":rel,**problem});continue
    files.append(f)
    sloc[rel]=set(parsed.module.sloc_lines)
    try:signatures[rel]=typed_signatures(parsed)
    except Exception:failures.append({"path":rel,"stage":"signature-extraction","code":"signature-extraction-failure","line":1,"column":0})
   with contextlib.redirect_stdout(sys.stderr):result=pipe.analyze_files(tuple(sorted(files)),include_all=True,disable_sg=True)
 except Exception as e:
  fail(f"analyzer failed: {e}")
 finally:
  pipe.dispatch_parse_source_file=original
 funcs=[]
 for f in result.flags.findings.all_functions:
  p=pm.get(f.file.resolve())
  if p:funcs.append({"path":p,"owner":identity_owner(p,f.owner_qualified_name),"name":f.name,"signature":signatures.get(p,{}).get((f.start_line,f.name)),"startLine":f.start_line,"endLine":f.end_line,"sloc":f.sloc,"cc":f.cyc_complexity,"mass":f.cyc_complexity*math.sqrt(f.sloc)})
 funcs.sort(key=lambda x:(x["path"],x["startLine"],x["name"]))
 inst=[];seen=set()
 for c in result.flags.findings.clones:
  p=pm.get(c.file.resolve());k=(c.group_hash,p,c.start_line,c.end_line)
  if not p or k in seen:continue
  seen.add(k); norm=entries[p][1].replace(b"\r\n",b"\n").replace(b"\r",b"\n");span=b"".join(norm.splitlines(keepends=True)[c.start_line-1:c.end_line])
  inst.append({"groupHash":c.group_hash,"path":p,"startLine":c.start_line,"endLine":c.end_line,"sloc":sum(i in sloc.get(p,set()) for i in range(c.start_line,c.end_line+1)),"sourceSha256":digest(span),"occurrenceOrdinal":0})
 inst.sort(key=lambda x:(x["groupHash"],x["path"],x["startLine"],x["endLine"])); ordinal=defaultdict(int)
 for x in inst:x["occurrenceOrdinal"],ordinal[x["groupHash"]]=ordinal[x["groupHash"]],ordinal[x["groupHash"]]+1
 lines=((x["path"],i) for x in inst for i in range(x["startLine"],x["endLine"]+1)); cm=metrics(funcs,lines,sloc)
 files=[{"path":p,"area":area(p),"metrics":metrics((x for x in funcs if x["path"]==p),((x["path"],i) for x in inst if x["path"]==p for i in range(x["startLine"],x["endLine"]+1)),{p:sloc[p]})} for p in sorted(sloc)]
 areas=area_rows(files);out=outlier_buckets(files,areas,cm)
 groups=[{"groupHash":g,"instances":[x for x in inst if x["groupHash"]==g]} for g in sorted(ordinal)]
 return {"corpusManifest":[entries[p][0] for p in sorted(entries)],"corpusCounts":{"supported":len(entries),"parsed":len(sloc),"omitted":len(entries)-len(sloc)},"skips":[{"path":p,"code":"upstream-omitted"} for p in sorted(set(entries)-set(sloc))],"corpusMetrics":cm,"files":files,"areas":areas,"outliers":out,"cloneGroups":groups,"highComplexityFunctions":[x for x in funcs if x["cc"]>10],"_functions":funcs,"_parsed":set(sloc),"_instances":inst,"_failures":failures}
def restrict(v:dict[str,Any],paths:set[str])->dict[str,Any]:
 fs=[x for x in v["files"] if x["path"] in paths]; return {"targetManifest":[x for x in v["corpusManifest"] if x["path"] in paths],"targetCounts":{"supported":len(paths),"parsed":len(v["_parsed"]&paths),"omitted":len(paths-v["_parsed"])},"targetMetrics":aggregate(fs),"targetOutliers":outlier_buckets(fs,area_rows(fs),v["corpusMetrics"]),"_failures":[x for x in v["_failures"] if x["path"] in paths]}
def capture_view(corpus:dict[str,Any],target:dict[str,Any])->dict[str,Any]:
 return {"corpusManifest":corpus["corpusManifest"],"targetManifest":target["targetManifest"],"corpusCounts":corpus["corpusCounts"],"targetCounts":target["targetCounts"],"skips":corpus["skips"],"corpusMetrics":corpus["corpusMetrics"],"targetMetrics":target["targetMetrics"],"files":corpus["files"],"areas":corpus["areas"],"outliers":corpus["outliers"],"targetOutliers":target["targetOutliers"],"cloneGroups":corpus["cloneGroups"],"highComplexityFunctions":corpus["highComplexityFunctions"],"_functions":corpus["_functions"],"_parsed":corpus["_parsed"],"_instances":corpus["_instances"],"_targetFailures":target["_failures"]}
def deltas(b:dict[str,Any],c:dict[str,Any],reasons:list[str])->dict[str,Any]:
 out={}
 for n in ("structuralErosion","verbosity"):
  x,y=b[n],c[n]; rs=set(reasons)
  if not x["applicable"]:rs.add("baseline-inapplicable")
  if not y["applicable"]:rs.add("current-inapplicable")
  rs=sorted(rs);app=x["applicable"] and y["applicable"] and not rs
  out[n]={"baseline":x,"current":y,"valueDelta":None if not(x["applicable"]and y["applicable"]) else y["value"]-x["value"],"numeratorDelta":y["numerator"]-x["numerator"],"denominatorDelta":y["denominator"]-x["denominator"],"classification":("regressed" if y["value"]>x["value"] else "improved" if y["value"]<x["value"] else "unchanged") if app else None,"suppressionReasons":rs}
 return out
def clone_key(x:dict[str,Any])->tuple[str,str,int]:return x["groupHash"],x["sourceSha256"],x["sloc"]
def clone_record(key:tuple[str,str,int],baseline:dict[str,Any]|None,current:dict[str,Any]|None)->dict[str,Any]:return {"key":{"groupHash":key[0],"sourceSha256":key[1],"sloc":key[2]},"baseline":baseline,"current":current}
def clone_changes(base:dict[str,Any],cur:dict[str,Any],pairs:list[dict[str,Any]])->tuple[list[Any],list[Any]]:
 target_baseline={pair["baseline"]["path"] for pair in pairs if pair["baseline"] is not None};target_current={pair["current"]["path"] for pair in pairs if pair["current"] is not None};target=[];relevant=set()
 for pair in pairs:
  bp=(pair["baseline"]or{}).get("path");cp=(pair["current"]or{}).get("path")
  bi=sorted((x for x in base["_instances"] if x["path"]==bp),key=lambda x:(x["groupHash"],x["sourceSha256"],x["sloc"],x["startLine"]))
  ci=sorted((x for x in cur["_instances"] if x["path"]==cp),key=lambda x:(x["groupHash"],x["sourceSha256"],x["sloc"],x["startLine"]))
  for key in sorted({clone_key(x) for x in bi+ci}):
   relevant.add(key[0]);left=[x for x in bi if clone_key(x)==key];right=[x for x in ci if clone_key(x)==key]
   for i in range(max(len(left),len(right))):target.append(clone_record(key,left[i] if i<len(left) else None,right[i] if i<len(right) else None))
 external=[]
 for group_hash in sorted(relevant):
  bi=[x for x in base["_instances"] if x["groupHash"]==group_hash and x["path"] not in target_baseline]
  ci=[x for x in cur["_instances"] if x["groupHash"]==group_hash and x["path"] not in target_current]
  left={(clone_key(x),x["path"],x["startLine"],x["endLine"]):x for x in bi};right={(clone_key(x),x["path"],x["startLine"],x["endLine"]):x for x in ci}
  for stable in sorted(set(left)&set(right)):
   key=stable[0];external.append(clone_record(key,left[stable],right[stable]))
 records=target+external
 records.sort(key=lambda x:(x["key"]["groupHash"],x["key"]["sourceSha256"],x["key"]["sloc"],(x["baseline"]or x["current"])["path"],(x["baseline"]or x["current"])["startLine"],(x["baseline"]or x["current"])["endLine"],0 if x["baseline"] is not None else 1,0 if x["current"] is not None else 1))
 groups=[{"groupHash":g,"change":"unchanged" if any(x["groupHash"]==g for x in base["_instances"]) and any(x["groupHash"]==g for x in cur["_instances"]) else "added" if any(x["groupHash"]==g for x in cur["_instances"]) else "removed"} for g in sorted(relevant)]
 return records,groups
def matched_changes(base:dict[str,Any],cur:dict[str,Any],pairs:list[dict[str,Any]])->list[Any]:
 functions=[]
 for pair in pairs:
  bp=(pair["baseline"]or{}).get("path");cp=(pair["current"]or{}).get("path")
  def grouped(xs):
   d=defaultdict(list)
   for x in xs:d[(x["owner"],x["name"],x["signature"])].append(x)
   return d
  bf,cf=grouped([x for x in base["_functions"] if x["path"]==bp]),grouped([x for x in cur["_functions"] if x["path"]==cp])
  overloaded={key[:2] for key in set(bf)|set(cf) if sum(len(v) for k,v in bf.items() if k[:2]==key[:2])+sum(len(v) for k,v in cf.items() if k[:2]==key[:2])>2}
  for key in sorted(set(bf)|set(cf),key=str):
   l,r=bf.get(key,[]),cf.get(key,[])
   if key[2] is None or key[:2] in overloaded or len(l)>1 or len(r)>1:functions.append({"identity":{"owner":key[0],"name":key[1],"signature":key[2]},"ambiguous":True,"baseline":None,"current":None});continue
   a,b=(l[0] if l else None),(r[0] if r else None)
   if (a and a["cc"]>10) or (b and b["cc"]>10):functions.append({"identity":{"owner":key[0],"name":key[1],"signature":key[2]},"ambiguous":False,"baseline":a,"current":b,"deltaCc":None if not a or not b else b["cc"]-a["cc"]})
 return functions
def targets(path:Path)->list[str]:
 try:r=json.loads(path.read_text(encoding="utf-8"))
 except Exception as e:fail(f"invalid targets file: {e}")
 if not isinstance(r,dict) or set(r)!={"schemaVersion","paths"} or r["schemaVersion"]!=TARGETS_SCHEMA or not isinstance(r["paths"],list):fail("targets schema invalid")
 out=[]
 for raw in r["paths"]:
  p=canon(raw)
  if p!=raw:fail("target path must be canonical relative POSIX")
  if out and p<=out[-1]:fail("target paths must be unique and ordinal-sorted")
  out.append(p)
 return out
def derive_pairs(listed:list[str],base:dict[str,tuple[dict[str,str],bytes]],cur:dict[str,tuple[dict[str,str],bytes]],capture_root:Path)->list[dict[str,Any]]:
 for p in listed:
  if pure_glsl_header(p):fail(f"target is not classified as C++ for BrokenEngineExtended: {p}")
  if p not in base and p not in cur:fail(f"target path is in neither the baseline nor the current corpus: {p}")
 pairs=[{"baseline":dict(base[p][0]),"current":dict(cur[p][0])} for p in listed if p in base and p in cur]
 unmatched=[p for p in listed if p in base and p not in cur]
 for cp in [p for p in listed if p in cur and p not in base]:
  # First ascending-ordinal rename match wins and is consumed, so pairing is deterministic.
  bp=next((x for x in unmatched if rename_pair(base[x][1],cur[cp][1],capture_root)),None)
  if bp is not None:unmatched.remove(bp)
  pairs.append({"baseline":None if bp is None else dict(base[bp][0]),"current":dict(cur[cp][0])})
 pairs.extend({"baseline":dict(base[p][0]),"current":None} for p in unmatched)
 pairs.sort(key=lambda pair:tuple((pair[s] or {}).get("path","") for s in ("baseline","current")))
 return pairs
def canonical_tool(tool:dict[str,Any])->dict[str,Any]:
 python=tool["python"]
 return {"adapterVersion":tool["adapterVersion"],"lockSha256":tool["lockSha256"],"python":{"implementation":python["implementation"],"version":python["version"],"architecture":python["architecture"],"executableSha256":python["executableSha256"]},"disableSg":tool["disableSg"]}
def common_metrics(v:dict[str,Any],paths:set[str])->dict[str,Any]:
 fs=[x for x in v["files"] if x["path"] in paths];return aggregate(fs)
def build(q:dict[str,Any])->dict[str,Any]:
 if set(q)!={"mode","repositoryRoot","captureRoot","analyzerSource","tool"}|({"target","scope"} if q.get("mode")=="Snapshot" else {"targets","baseline"}):fail("unknown or missing request field")
 root=Path(q["repositoryRoot"]);cap=Path(q["captureRoot"])
 if not root.is_absolute() or not cap.is_absolute() or not cap.is_dir():fail("invalid capture root")
 check_components(root,cap,False)
 if not isinstance(q["tool"],dict) or set(q["tool"])!={"adapterVersion","lockSha256","python","disableSg"} or q["tool"]["adapterVersion"]!="5":fail("invalid tool identity")
 if not isinstance(q["tool"]["python"],dict) or set(q["tool"]["python"])!={"implementation","version","architecture","executableSha256"}:fail("invalid python identity")
 analyzer_source=validate_analyzer_source(root,q["analyzerSource"])
 before=current(root)
 if q["mode"]=="Snapshot":
  target=canon(q["target"]);p=root.joinpath(*target.split("/"))
  if q["scope"]=="Exact" and pure_glsl_header(target):fail(f"target is not classified as C++ for BrokenEngineExtended: {target}")
  if q["scope"]=="Exact": paths={target} if target in before else fail("Exact target not discovered")
  else:
   check_components(root,p,False)
   if not p.is_dir() or p.is_symlink() or getattr(p.lstat(),"st_file_attributes",0)&0x400:fail("directory target must exist and be ordinary")
   prefix=target+"/";paths={x for x in before if x.startswith(prefix) and (q["scope"]=="Recursive" or "/" not in x[len(prefix):])}
  cv=capture(before,cap,analyzer_source);cur=capture_view(cv,restrict(cv,paths));base=None;comparison=None;select={"kind":"snapshot","scope":q["scope"],"target":target,"paths":sorted(paths)}
 elif q["mode"]=="Compare":
  br=baseline(root,q["baseline"]);pairs=derive_pairs(targets(Path(q["targets"])),br,before,cap)
  bt={pair["baseline"]["path"] for pair in pairs if pair["baseline"] is not None};ct={pair["current"]["path"] for pair in pairs if pair["current"] is not None}
  bv=capture(br,cap,analyzer_source);cv_c=capture(before,cap,analyzer_source);base=capture_view(bv,restrict(bv,bt));cur=capture_view(cv_c,restrict(cv_c,ct))
  failures=[]
  for side,view in (("baseline",base),("current",cur)):
   failures.extend({"side":side,"path":failure["path"],"stage":failure["stage"],"code":failure["code"],"line":failure["line"],"column":failure["column"]} for failure in view["_targetFailures"])
  failures.sort(key=lambda x:(0 if x["side"]=="baseline" else 1,x["path"],x["line"],x["column"],x["stage"],x["code"]))
  dispatch_failures=[x for x in failures if x["stage"]=="dispatch-parse"]
  if dispatch_failures:raise TargetMetricFailure("target-parse-failure","sanitize the listed target spelling and rerun Compare",dispatch_failures)
  if failures:raise TargetMetricFailure("target-signature-extraction-failure","investigate target signature extraction and rerun Compare",failures)
  context=[{"path":p,"change":"added" if p not in br else "removed" if p not in before else "modified"} for p in sorted(set(br)|set(before)) if p not in bt and p not in ct and br.get(p,(None,))[0]!=before.get(p,(None,))[0]]
  pair_states=[((pair["baseline"]or{}).get("path"),(pair["current"]or{}).get("path")) for pair in pairs]
  logical_membership=any(not bp or not cp for bp,cp in pair_states) or any(x["change"] in {"added","removed"} for x in context)
  parse_change=any((bp in base["_parsed"] if bp else False)!=(cp in cur["_parsed"] if cp else False) for bp,cp in pair_states)
  target_reasons=sorted(set((["context-change"] if context else [])+(["membership-change"] if logical_membership else [])+(["parse-status-change"] if parse_change else [])))
  corpus_reasons=target_reasons
  cohort_pairs=[(bp,cp) for bp,cp in pair_states if bp and cp and bp in base["_parsed"] and cp in cur["_parsed"]]
  cohort_base={bp for bp,_ in cohort_pairs};cohort_current={cp for _,cp in cohort_pairs};cohort_reasons=sorted({"context-change"} if context else set())
  clones,groups=clone_changes(base,cur,pairs);functions=matched_changes(base,cur,pairs)
  comparison={"contextChanges":context,"corpus":deltas(base["corpusMetrics"],cur["corpusMetrics"],corpus_reasons),"target":deltas(base["targetMetrics"],cur["targetMetrics"],target_reasons),"commonParsedCohort":{"baselinePaths":sorted(cohort_base),"currentPaths":sorted(cohort_current),"metrics":deltas(common_metrics(base,cohort_base),common_metrics(cur,cohort_current),cohort_reasons),"suppressionReasons":cohort_reasons},"coverage":{"baseline":{"corpus":base["corpusCounts"],"target":base["targetCounts"]},"current":{"corpus":cur["corpusCounts"],"target":cur["targetCounts"]},"deltas":{"corpus":{k:cur["corpusCounts"][k]-base["corpusCounts"][k] for k in base["corpusCounts"]},"target":{k:cur["targetCounts"][k]-base["targetCounts"][k] for k in base["targetCounts"]}}},"cloneGroups":groups,"cloneInstances":clones,"functions":functions};select={"kind":"manifest","pairs":pairs,"paths":sorted(ct)}
 else:fail("invalid mode")
 after=current(root)
 if {p:x[0] for p,x in before.items()}!={p:x[0] for p,x in after.items()}:fail("capture drift")
 def pub(v:Any)->Any:return None if v is None else {k:x for k,x in v.items() if not k.startswith("_")}
 return {"schemaVersion":SCHEMA,"mode":q["mode"],"profile":PROFILE,"tool":canonical_tool(q["tool"]),"targetSelection":select,"baseline":pub(base),"current":pub(cur),"comparison":comparison}
def norm(x:Any)->Any:
 if isinstance(x,float):return 0 if round(x,12)==0 else round(x,12)
 if isinstance(x,dict):return {k:norm(v) for k,v in x.items()}
 if isinstance(x,list):return [norm(i) for i in x]
 return x
def main()->int:
 a=argparse.ArgumentParser();a.add_argument("--request",required=True);z=a.parse_args()
 try:
  r=build(json.loads(Path(z.request).read_text(encoding="utf-8")));o={k:norm(r[k]) for k in ("schemaVersion","mode","profile","tool","targetSelection","baseline","current","comparison")};sys.stdout.write(json.dumps(o,separators=(",",":"),ensure_ascii=False,allow_nan=False)+"\n");return 0
 except TargetMetricFailure as e:print(json.dumps({"code":e.code,"message":e.message,"failures":e.failures},separators=(",",":"),ensure_ascii=False,allow_nan=False),file=sys.stderr);return 2
 except (MetricsError,OSError,ValueError,KeyError,TypeError) as e:print("CodeQualityMetrics: "+str(e),file=sys.stderr);return 2
if __name__=="__main__":raise SystemExit(main())
