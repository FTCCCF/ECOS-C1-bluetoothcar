"""Passively record live C1 neural results. Never write to the serial port."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import time
import serial

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--identity-report',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=60,choices=range(5,61))
    p.add_argument('--port',default='COM5')
    p.add_argument('--label',default='user-sequences')
    args=p.parse_args()
    identity=json.loads(args.identity_report.read_text('utf-8-sig'))
    if not identity.get('continuous_acknowledged'):raise ValueError('Expected acknowledged continuous listening.')
    if not re.fullmatch(r'[A-Za-z0-9_-]+',args.label):raise ValueError('Invalid label.')
    base=ROOT/'tests/results/diagnostic'/(datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+args.label)
    cancel=base.with_suffix('.cancel')
    control=ROOT/'tmp/voice-observer-control.json';control.parent.mkdir(parents=True,exist_ok=True)
    report=dict(started_at=datetime.now().astimezone().isoformat(),port=args.port,serial_bytes_written=0,
                identity_source=str(args.identity_report.resolve()),
                identity_source_sha256=hashlib.sha256(args.identity_report.read_bytes()).hexdigest(),
                model_sha256=identity['model_sha256'],firmware_id=identity['firmware_id'],
                physical_motion_verified=False,events=[],results=[],neural_stop_transitions=[])
    port=serial.Serial(port=None,baudrate=115200,timeout=.25,dsrdtr=False,rtscts=False)
    port.port=args.port;port.dtr=False;port.rts=False
    moving=None
    try:
        port.open()
        control.write_text(json.dumps(dict(active=True,cancel_file=str(cancel),report=str(base.with_suffix('.json')))),encoding='utf-8')
        print('OBSERVER_READY serial_reads_only seconds='+str(args.seconds),flush=True)
        started=time.monotonic()
        while time.monotonic()-started<args.seconds and not cancel.exists():
            raw=port.readline()
            if not raw:continue
            line=raw.decode('ascii').rstrip('\r\n')
            event=dict(timestamp=datetime.now().astimezone().isoformat(),text=line)
            report['events'].append(event);print(line,flush=True)
            if line.startswith('MODEL_SHA256=') and line.split('=',1)[1]!=identity['model_sha256']:
                raise ValueError('Live model identity changed during observation.')
            if not line.startswith('RESULT '):continue
            result=dict(re.findall(r'(\w+)=(\w+)',line))
            result['timestamp']=event['timestamp'];report['results'].append(result)
            index=len(report['results'])-1
            decision=result['decision'];a=int(result['CR1_A']);b=int(result['CR2_B'])
            if decision in ('forward','backward'):
                if a!=0 or b!=0:raise ValueError('Accepted movement without full-duty outputs.')
                moving=(decision,index)
            elif decision=='stop':
                if a!=20000 or b!=20000:raise ValueError('Accepted stop without stopped outputs.')
                if moving:
                    report['neural_stop_transitions'].append(dict(movement=moving[0],
                        movement_result_index=moving[1],stop_result_index=index,
                        no_host_command_between=True))
                moving=None
        report['cancelled']=cancel.exists()
    except Exception as error:
        report['error']=str(error)
        raise
    finally:
        if port.is_open:port.close()
        report['ended_at']=datetime.now().astimezone().isoformat()
        base.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        control.write_text(json.dumps(dict(active=False,cancel_file=str(cancel),report=str(base.with_suffix('.json')))),encoding='utf-8')
        print('Saved: '+str(base.with_suffix('.json')),flush=True)
        print('Neural stop transitions: '+json.dumps(report['neural_stop_transitions']),flush=True)


if __name__=='__main__':main()
