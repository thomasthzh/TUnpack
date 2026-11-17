"""Stage/apply/roll back this website's existing Caddy route, never global config.

Uses the operator's existing SSH alias. No credentials, installers, DNS changes,
deletion, service restart, or unrelated route changes. Default phase is inspect.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shlex
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
REMOTE = r'''
import copy,json,os,pathlib,shutil,sys,tarfile,urllib.request
phase,release=sys.argv[1:]
assert phase in ['inspect','stage','apply','rollback']
assert release.replace('-','').isalnum()
base=pathlib.Path('/srv/tunpack-site')
new=base/'releases'/release
ops=base/'operations'
route_id='thomas-tunpack-https'
endpoint='http://127.0.0.1:2019/'
def get(path):
    return json.load(urllib.request.urlopen(endpoint+path,timeout=20))
def get_route():
    with urllib.request.urlopen(endpoint+'id/'+route_id,timeout=20) as response:
        value=json.load(response);etag=response.headers.get('ETag');assert etag
        return value,etag
def set_route(value,etag):
    request=urllib.request.Request(endpoint+'id/'+route_id,data=json.dumps(value).encode(),headers={'Content-Type':'application/json','If-Match':etag},method='PATCH')
    with urllib.request.urlopen(request,timeout=30) as response:assert response.status==200
def find_id(value):
    if isinstance(value,dict):
        if value.get('@id')==route_id:return value
        for item in value.values():
            found=find_id(item)
            if found is not None:return found
    if isinstance(value,list):
        for item in value:
            found=find_id(item)
            if found is not None:return found
def excluding_ours(value):
    if isinstance(value,dict):
        return {'@id':route_id} if value.get('@id')==route_id else {key:excluding_ours(item) for key,item in value.items()}
    return [excluding_ours(item) for item in value] if isinstance(value,list) else value
before,before_etag=get_route()
assert before['match']==[{'host':['tunpack.thomasthzh.top']}]
assert before['terminal'] is True
file_server=before['handle'][0]['routes'][1]['handle'][1]
assert file_server['handler']=='file_server'
old=pathlib.Path(file_server['root'])
assert old.is_relative_to(base/'releases') and old.is_dir()
if phase=='inspect':
    print(json.dumps({'host':'tunpack.thomasthzh.top','old_root':str(old),'new_root':str(new),'method':'targeted Caddy route PATCH; no DNS or global reload'}));sys.exit()
if phase=='stage':
    assert not new.exists()
    new.mkdir(parents=True)
    with tarfile.open(base/'incoming'/(release+'.tar')) as source:
        for member in source.getmembers():
            assert member.isfile()
            target=new/member.name
            assert target.resolve().is_relative_to(new.resolve())
            target.parent.mkdir(parents=True,exist_ok=True)
            with source.extractfile(member) as stream,target.open('wb') as output:shutil.copyfileobj(stream,output)
    # Retain already-public historical assets for cached pages. No source removed.
    if (old/'assets').is_dir():
        for source in (old/'assets').rglob('*'):
            target=new/'assets'/source.relative_to(old/'assets')
            if source.is_file() and not source.is_symlink() and not target.exists():
                target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    for name in ['app.js','manifest.webmanifest','robots.txt','sitemap.xml']:
        if (old/name).is_file() and not (new/name).exists():shutil.copyfile(old/name,new/name)
    assert (new/'index.html').is_file() and (new/'assets/hero-motion-20261117-wide.mp4').stat().st_size>100000
    desired=copy.deepcopy(before)
    desired['handle'][0]['routes'][1]['handle'][1]['root']=str(new)
    headers=desired['handle'][0]['routes'][0]['handle'][0]['response']['set']
    policy=headers['Content-Security-Policy'][0]
    # The previous deployment established this policy. This update only switches
    # the file root; a changed server policy requires a fresh explicit review.
    assert "media-src 'self'" in policy
    ops.mkdir(exist_ok=True)
    (ops/(release+'-before.json')).write_text(json.dumps(before))
    (ops/(release+'-after.json')).write_text(json.dumps(desired))
    print(json.dumps({'staged':str(new),'old_root_retained':str(old),'files':sum(1 for x in new.rglob('*') if x.is_file()),'switch_applied':False}));sys.exit()
saved=json.loads((ops/(release+'-before.json')).read_text())
desired=json.loads((ops/(release+'-after.json')).read_text())
source,target=(saved,desired) if phase=='apply' else (desired,saved)
assert before==source, 'Selected site changed since stage; inspect before retrying'
whole_before=get('config/')
set_route(target,before_etag)
after=get('id/'+route_id)
assert after==target
assert excluding_ours(whole_before)==excluding_ours(get('config/'))
autosave=pathlib.Path('/var/lib/caddy/.config/caddy/autosave.json')
assert find_id(json.loads(autosave.read_text()))==target
print(json.dumps({'phase':phase,'host':'tunpack.thomasthzh.top','active_root':target['handle'][0]['routes'][1]['handle'][1]['root'],'other_configuration_unchanged':True,'autosave_persisted':True,'previous_release_retained':True}))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssh-host', required=True)
    parser.add_argument('--phase', choices=['inspect','stage','apply','rollback'], default='inspect')
    parser.add_argument('--release', default='hero-'+datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S'))
    args = parser.parse_args()
    assert args.release.replace("-", "").isalnum(), "Use a plain release identifier"
    ssh = ['ssh','-T','-o','BatchMode=yes','-o','ConnectTimeout=12',args.ssh_host]
    if args.phase=='stage':
        target=ROOT/'docs/evidence/hero-motion'/(args.release+'.tar')
        target.parent.mkdir(parents=True,exist_ok=True)
        with tarfile.open(target,'w') as archive:
            for file in sorted((ROOT/'site').rglob('*')):
                if file.is_file():archive.add(file,arcname=file.relative_to(ROOT/'site').as_posix(),recursive=False)
            # Legacy official download URL now serves the same licensed release.
            archive.add(ROOT/'dist/TUnpack-0.13.0-windows-x64.zip',arcname='downloads/T解压-portable.zip',recursive=False)
        subprocess.run(ssh+['mkdir -p /srv/tunpack-site/incoming'],check=True)
        subprocess.run(['scp','-o','BatchMode=yes','-o','ConnectTimeout=12',str(target),args.ssh_host+':/srv/tunpack-site/incoming/'+args.release+'.tar'],check=True)
    command='python3 - '+shlex.quote(args.phase)+' '+shlex.quote(args.release)
    result=subprocess.run(ssh+[command],input=REMOTE.encode(),capture_output=True,check=True)
    print(result.stdout.decode().strip())


if __name__=='__main__':
    main()
