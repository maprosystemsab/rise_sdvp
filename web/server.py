import html
import json
import os
import socket
import subprocess
import sqlite3
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor
from flask import Flask, Response, request, send_from_directory

app = Flask(__name__)


# --- Kunder: var kund ser bara sina egna robotar och gårdar -----------------------
# kunder.json (bredvid server.py) talar om vilka WireGuard-adresser som är kunddatorer
# och vilka robotar/gårdar som hör till dem. Frågor från alla andra adresser (våra
# egna) ser allt som förut. Filen läses vid varje fråga, så ändringar gäller direkt.
KUNDFIL = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kunder.json')

# Det här får en kunddator göra (allt annat nekas): läsa listorna och sina egna
# robotar/gårdar. Ändra/lägga till/ta bort, fält, banor och loggar är bara för oss.
KUND_TILLATET = {'hello_world', 'robotar', 'list_machines', 'all_machines', 'vehicle_types',
                 'all_farms', 'read_machine', 'read_farm'}


def las_kunder():
    try:
        with open(KUNDFIL) as f:
            return json.load(f).get('kunder', {})
    except FileNotFoundError:
        return {}


def kund_for(ip):
    """Kunden som äger datorn med den här adressen, eller None (= vi själva, ser allt)."""
    for namn, k in las_kunder().items():
        if ip in k.get('datorer', []):
            return dict(k, namn=namn)
    return None


def fragande_kund():
    return kund_for(request.remote_addr or '')


def maskin_tillhor(kund, ip):
    return kund is None or ip in kund.get('robotar', [])


def gard_tillhor(kund, gard_id):
    return kund is None or str(gard_id) in [str(g) for g in kund.get('gardar', [])]


@app.before_request
def kundsparr():
    kund = fragande_kund()
    if kund is None:
        return None
    if request.endpoint not in KUND_TILLATET:
        return Response('Inte tillgängligt för kunddatorer', status=403)
    obj_id = request.form.get('id') or request.args.get('id')
    if request.endpoint == 'read_machine' and obj_id:
        conn = sqlite3.connect('data.db')
        rad = conn.execute('SELECT ip FROM machines WHERE id = ?', (obj_id,)).fetchone()
        conn.close()
        if rad is None or not maskin_tillhor(kund, rad[0]):
            return Response(f'Error: No machine found with id {obj_id}', status=404)
    if request.endpoint == 'read_farm' and obj_id and not gard_tillhor(kund, obj_id):
        return Response(f'Error: No farm found with id {obj_id}', status=404)
    return None


def svarar_pa_ping(ip, forsok=3):
    """Pingar en robot flera gånger (4G har långa och ojämna svarstider)."""
    for _ in range(forsok):
        r = subprocess.run(['ping', '-c', '1', '-W', '2', ip],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            return True
    return False


@app.route('/')
def hello_world():
    return "Hello, World!"


def har_webbstyrning(ip, port=9443, tid=3.0):
    """Svarar robotens webbsida för telefon/iPad/Quest (robotd webb.rs)?"""
    try:
        with socket.create_connection((ip, port), timeout=tid):
            return True
    except OSError:
        return False


ROBOTSIDA = """<!doctype html>
<html lang="sv"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<title>Robotar</title>
<style>
 :root{color-scheme:dark}
 body{margin:0;background:#0f1412;color:#e6ece9;font:16px/1.4 -apple-system,system-ui,sans-serif;
   padding:max(16px,env(safe-area-inset-top)) 16px 32px}
 h1{font-size:1.5rem;margin:.2em 0 .8em}
 .lista{display:grid;gap:12px;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));max-width:1000px}
 .robot{background:#18201d;border:1px solid #2e3a35;border-radius:14px;padding:14px 16px;
   display:flex;align-items:center;gap:12px;text-decoration:none;color:inherit}
 .prick{width:14px;height:14px;border-radius:50%;flex:none}
 .pa .prick{background:#3ecf7a}.gammal .prick{background:#f2c25a}.av .prick{background:#55605b}
 .namn{font-weight:600;font-size:1.1rem}.info{color:#9aa7a1;font-size:.9rem}
 .kor{margin-left:auto;background:#2b6b4f;border-radius:10px;padding:10px 16px;font-weight:600;flex:none}
 .av{opacity:.55}
 p.tips{color:#9aa7a1;font-size:.9rem;max-width:60ch}
</style></head><body>
<h1>Robotar</h1>
<div class="lista">{{LISTA}}</div>
<p class="tips">Första gången en robot öppnas varnar webbläsaren för certifikatet: välj <b>Visa detaljer</b>
(eller <b>Avancerat</b>) och fortsätt till sidan. Uppdatera listan genom att ladda om sidan.</p>
</body></html>"""


@app.route('/robotar')
def robotar():
    """Startsida för telefon/iPad/Quest: robotarna man får se, med länk till körsidan."""
    kund = fragande_kund()
    conn = sqlite3.connect('data.db')
    rader = conn.execute('SELECT name, ip FROM machines').fetchall()
    conn.close()
    rader = sorted((r for r in rader if r[1] and maskin_tillhor(kund, r[1])), key=lambda r: r[0].lower())

    def kolla(rad):
        if har_webbstyrning(rad[1]):
            return 'pa'
        return 'gammal' if svarar_pa_ping(rad[1], forsok=1) else 'av'

    with ThreadPoolExecutor(max_workers=32) as ex:
        lagen = list(ex.map(kolla, rader))

    kort = []
    for (namn, ip), lage in sorted(zip(rader, lagen), key=lambda x: ({'pa': 0, 'gammal': 1, 'av': 2}[x[1]], x[0][0].lower())):
        n, i = html.escape(namn), html.escape(ip)
        if lage == 'pa':
            kort.append(f'<a class="robot pa" href="https://{i}:9443/"><span class="prick"></span>'
                        f'<span><div class="namn">{n}</div><div class="info">{i}</div></span><span class="kor">Kör</span></a>')
        else:
            text = 'på, men saknar telefonstyrning' if lage == 'gammal' else 'svarar inte'
            kort.append(f'<div class="robot {lage}"><span class="prick"></span>'
                        f'<span><div class="namn">{n}</div><div class="info">{i} · {text}</div></span></div>')
    if not kort:
        kort.append('<div class="robot av"><span>Inga robotar.</span></div>')
    return Response(ROBOTSIDA.replace('{{LISTA}}', ''.join(kort)), mimetype='text/html')


@app.route('/leafletLoadXML.html')
def serve_leaflet():
    return send_from_directory('.', 'leafletLoadXML.html')


@app.route('/fields/<path:filename>')
def serve_fields(filename):
    return send_from_directory('fields', filename)


@app.route('/field/<path:filename>')
def serve_field(filename):
    return send_from_directory('fields', filename)


@app.route('/paths/<path:filename>')
def serve_paths(filename):
    return send_from_directory('paths', filename)


@app.route('/machines')
def list_machines():
    """Registrerade robotar som svarar just nu (för kunder: bara deras egna).

    Förut: nmap över hela 192.168.200.0/24 med 0,8 s svarstid och 12 s totalt, vilket
    över 4G ibland missade robotar eller gav tom lista. Nu pingas bara de registrerade
    robotarna, parallellt och med tre försök à 2 s.
    """
    try:
        kund = fragande_kund()
        conn = sqlite3.connect('data.db')
        rader = conn.execute('SELECT name, ip, iVehicletype FROM machines').fetchall()
        conn.close()
        rader = [r for r in rader if r[1] and maskin_tillhor(kund, r[1])]

        with ThreadPoolExecutor(max_workers=32) as ex:
            svar = list(ex.map(lambda r: svarar_pa_ping(r[1]), rader))

        root = ET.Element('machines')
        for (name, ip, vtyp), lever in sorted(zip(rader, svar), key=lambda x: x[0][1]):
            if not lever:
                continue
            machine_elem = ET.SubElement(root, 'machine')
            ET.SubElement(machine_elem, 'name').text = name
            ET.SubElement(machine_elem, 'ip').text = ip
            if vtyp is not None:
                ET.SubElement(machine_elem, 'iVehicletype').text = str(vtyp)

        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        xml_str = xml_str.replace('<machines />', '<machines></machines>')
        return Response(xml_str, mimetype='application/xml')
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/all_machines')
def all_machines():
    try:
        # Connect to SQLite database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Query all machines from the table
        cursor.execute('SELECT id, name, ip, iVehicletype FROM machines')
        machines = cursor.fetchall()
        conn.close()
        
        # Create XML structure
        root = ET.Element('machines')
        kund = fragande_kund()
        for machine_id, name, ip, iVehicletype in machines:
            if not maskin_tillhor(kund, ip):
                continue
            machine_elem = ET.SubElement(root, 'machine')
            ET.SubElement(machine_elem, 'id').text = str(machine_id)
            ET.SubElement(machine_elem, 'name').text = name
            ET.SubElement(machine_elem, 'ip').text = ip
            if iVehicletype is not None:
                ET.SubElement(machine_elem, 'iVehicletype').text = str(iVehicletype)
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        xml_str = xml_str.replace('<machines />', '<machines></machines>')
        if fragande_kund() is None:
            # Write to file
            with open('machines.xml', 'w') as f:
                f.write(xml_str)
        
        return Response(xml_str, mimetype='application/xml')
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/add_machine', methods=['POST'])
def add_machine():
    try:
        # Debug: Print request information
        print(f"DEBUG: Request method: {request.method}")
        print(f"DEBUG: Request form data: {dict(request.form)}")
        print(f"DEBUG: Request args (URL params): {dict(request.args)}")
        print(f"DEBUG: Request data: {request.data}")
        
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            print(f"DEBUG: get_param('{name}') = {value}")
            return value
        
        name = get_param('name')
        ip = get_param('ip')
        port = get_param('port')
        gearratio = get_param('gearratio')
        wheeldiameter_m = get_param('wheeldiameter_m')
        motorpoles = get_param('motorpoles')
        turnradius_m = get_param('turnradius_m')
        steeringramp = get_param('steeringramp')
        axisdistance_m = get_param('axisdistance_m')
        servocenter = get_param('servocenter')
        servorange = get_param('servorange')
        yawIMUgain = get_param('yawIMUgain')
        servoPgain = get_param('servoPgain')
        servoIgain = get_param('servoIgain')
        servoDgain = get_param('servoDgain')
        maxleft_degrees = get_param('maxleft_degrees')
        maxright_degrees = get_param('maxright_degrees')
        centervoltage_V = get_param('centervoltage_V')
        iVehicletype = get_param('iVehicletype') or get_param('vehicle_type_id')
        
        print(f"DEBUG: Final values - name: {name}, ip: {ip}, iVehicletype: {iVehicletype}")
        
        # Validate required fields
        if not name:
            print("DEBUG: Missing name field")
            return "Error: 'name' field is required", 400

        if not ip:
            print("DEBUG: Missing ip field")
            return "Error: 'ip' field is required", 400

        # Connect to database
        print("DEBUG: Connecting to database")
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Build the INSERT query with all fields
        columns = ['name']
        values = [name]
        placeholders = ['?']
        
        # Add optional fields if provided
        field_mappings = [
            ('ip', ip),
            ('port', port),
            ('gearratio', gearratio),
            ('wheeldiameter_m', wheeldiameter_m),
            ('motorpoles', motorpoles),
            ('turnradius_m', turnradius_m),
            ('steeringramp', steeringramp),
            ('axisdistance_m', axisdistance_m),
            ('servocenter', servocenter),
            ('servorange', servorange),
            ('yawIMUgain', yawIMUgain),
            ('servoPgain', servoPgain),
            ('servoIgain', servoIgain),
            ('servoDgain', servoDgain),
            ('maxleft_degrees', maxleft_degrees),
            ('maxright_degrees', maxright_degrees),
            ('centervoltage_V', centervoltage_V),
            ('iVehicletype', iVehicletype)
        ]
        
        for field_name, field_value in field_mappings:
            if field_value is not None:
                columns.append(field_name)
                values.append(field_value)
                placeholders.append('?')
        
        # Create and execute the INSERT statement
        columns_str = ', '.join(columns)
        placeholders_str = ', '.join(placeholders)
        query = f'INSERT INTO machines ({columns_str}) VALUES ({placeholders_str})'
        print(f"DEBUG: Executing query: {query}")
        print(f"DEBUG: With values: {values}")
        cursor.execute(query, values)
        print(f"DEBUG: Rows affected: {cursor.rowcount}")
        
        # Get the ID of the newly inserted machine
        machine_id = cursor.lastrowid
        
        # Log the addition to log_machines table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Build the machine data as a string for logging
        machine_data = {col: val for col, val in zip(columns, values)}
        machine_data_str = str(machine_data)
        
        cursor.execute(
            'INSERT INTO log_machines (machine_id, machine_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (machine_id, machine_data_str, timestamp, 'ADD')
        )
        
        conn.commit()
        conn.close()
        
        print("DEBUG: Machine added successfully")
        return "Machine added successfully", 201
        
    except sqlite3.IntegrityError as e:
        print(f"DEBUG: IntegrityError: {str(e)}")
        return f"Error: {str(e)}", 409
    except Exception as e:
        print(f"DEBUG: Exception: {str(e)}")
        import traceback
        print(f"DEBUG: Traceback: {traceback.format_exc()}")
        return f"Error: {str(e)}", 500


@app.route('/read_machine', methods=['GET', 'POST'])
def read_machine():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value

        machine_id = get_param('id')

        if not machine_id:
            return "Error: 'id' field is required", 400

        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()

        # Get the machine with the specified id
        cursor.execute('SELECT * FROM machines WHERE id = ?', (machine_id,))
        machine = cursor.fetchone()

        # Get column names
        column_names = [description[0] for description in cursor.description]
        conn.close()

        if machine is None:
            return f"Error: No machine found with id {machine_id}", 404

        # Create XML structure
        root = ET.Element('machines')
        machine_elem = ET.SubElement(root, 'machine')
        for i, column_name in enumerate(column_names):
            ET.SubElement(machine_elem, column_name).text = str(machine[i])

        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')

        return Response(xml_str, mimetype='application/xml')

    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/remove_machine', methods=['GET', 'POST'])
def remove_machine():
    try:
        # Get all the fields from POST form data or URL parameters (same as add_machine)
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        machine_id = get_param('id')
        
        if not machine_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the machine data before deleting
        cursor.execute('SELECT * FROM machines WHERE id = ?', (machine_id,))
        machine = cursor.fetchone()
        
        if machine is None:
            conn.close()
            return f"Error: No machine found with id {machine_id}", 404
        
        # Build machine data string for logging
        from datetime import datetime
        columns = [description[0] for description in cursor.description]
        machine_data = dict(zip(columns, machine))
        machine_data_str = str(machine_data)
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Log the removal to log_machines table
        cursor.execute(
            'INSERT INTO log_machines (machine_id, machine_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (machine_id, machine_data_str, timestamp, 'REMOVE')
        )
        
        # Delete the machine with the specified id
        cursor.execute('DELETE FROM machines WHERE id = ?', (machine_id,))
        
        conn.commit()
        conn.close()
        
        return f"Machine with id {machine_id} removed successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/vehicle_types')
def vehicle_types():
    try:
        # Connect to SQLite database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Query all vehicle types from the table using correct database column names (length, wtidth)
        cursor.execute('SELECT id, name, typeofsteering, length, wtidth FROM vehicle_types')
        vehicle_types = cursor.fetchall()
        conn.close()
        
        # Create XML structure
        root = ET.Element('vehicle_types')
        for vehicle_type in vehicle_types:
            vt_elem = ET.SubElement(root, 'vehicle_type')
            ET.SubElement(vt_elem, 'id').text = str(vehicle_type[0])
            ET.SubElement(vt_elem, 'name').text = str(vehicle_type[1])
            ET.SubElement(vt_elem, 'typeofsteering').text = str(vehicle_type[2])
            ET.SubElement(vt_elem, 'length_m').text = str(vehicle_type[3])
            ET.SubElement(vt_elem, 'width_m').text = str(vehicle_type[4])
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        # Write to file
        with open('vehicle_types.xml', 'w') as f:
            f.write(xml_str)
        
        return Response(xml_str, mimetype='application/xml')
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/all_farms')
def all_farms():
    try:
        # Connect to SQLite database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Query all fields from the locations table
        cursor.execute('SELECT * FROM locations')
        locations = cursor.fetchall()
        
        # Get column names from cursor description
        column_names = [description[0] for description in cursor.description]
        conn.close()
        
        # Create XML structure
        root = ET.Element('locations')
        kund = fragande_kund()
        id_kol = column_names.index('id') if 'id' in column_names else 0
        for location in locations:
            if not gard_tillhor(kund, location[id_kol]):
                continue
            location_elem = ET.SubElement(root, 'location')
            for i, column_name in enumerate(column_names):
                ET.SubElement(location_elem, column_name).text = str(location[i])
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        xml_str = xml_str.replace('<locations />', '<locations></locations>')
        if kund is None:
            # Write to file
            with open('locations.xml', 'w') as f:
                f.write(xml_str)
        
        return Response(xml_str, mimetype='application/xml')
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/add_farm', methods=['POST'])
def add_farm():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        # Get farm fields - adjust these based on your locations table schema
        name = get_param('name')
        
        # Validate required fields
        if not name:
            return "Error: 'name' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Build the INSERT query with all provided fields
        columns = ['name']
        values = [name]
        placeholders = ['?']
        
        # Add optional fields if provided
        field_mappings = [
            ('description', get_param('description')),
            ('latitude', get_param('latitude')),
            ('longitude', get_param('longitude')),
            ('area_ha', get_param('area_ha')),
            ('created_at', get_param('created_at')),
        ]
        
        for field_name, field_value in field_mappings:
            if field_value is not None:
                columns.append(field_name)
                values.append(field_value)
                placeholders.append('?')
        
        # Create and execute the INSERT statement
        columns_str = ', '.join(columns)
        placeholders_str = ', '.join(placeholders)
        query = f'INSERT INTO locations ({columns_str}) VALUES ({placeholders_str})'
        cursor.execute(query, values)
        
        # Get the ID of the newly inserted farm
        farm_id = cursor.lastrowid
        
        # Log the addition to log_farms table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Build the farm data as a string for logging
        farm_data = {col: val for col, val in zip(columns, values)}
        farm_data_str = str(farm_data)
        
        cursor.execute(
            'INSERT INTO log_farms (farm_id, farm_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (farm_id, farm_data_str, timestamp, 'ADD')
        )
        
        conn.commit()
        conn.close()
        
        return "Farm added successfully", 201
        
    except sqlite3.IntegrityError as e:
        return f"Error: {str(e)}", 409
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/read_farm', methods=['GET', 'POST'])
def read_farm():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value

        farm_id = get_param('id')

        if not farm_id:
            return "Error: 'id' field is required", 400

        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()

        # Get the farm with the specified id
        cursor.execute('SELECT * FROM locations WHERE id = ?', (farm_id,))
        farm = cursor.fetchone()

        # Get column names
        column_names = [description[0] for description in cursor.description]
        conn.close()

        if farm is None:
            return f"Error: No farm found with id {farm_id}", 404

        # Create XML structure
        root = ET.Element('locations')
        farm_elem = ET.SubElement(root, 'location')
        for i, column_name in enumerate(column_names):
            ET.SubElement(farm_elem, column_name).text = str(farm[i])

        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')

        return Response(xml_str, mimetype='application/xml')

    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/remove_farm', methods=['GET', 'POST'])
def remove_farm():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        farm_id = get_param('id')
        
        if not farm_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the farm data before deleting
        cursor.execute('SELECT * FROM locations WHERE id = ?', (farm_id,))
        farm = cursor.fetchone()
        
        if farm is None:
            conn.close()
            return f"Error: No farm found with id {farm_id}", 404
        
        # Build farm data string for logging
        from datetime import datetime
        columns = [description[0] for description in cursor.description]
        farm_data = dict(zip(columns, farm))
        farm_data_str = str(farm_data)
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Log the removal to log_farms table
        cursor.execute(
            'INSERT INTO log_farms (farm_id, farm_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (farm_id, farm_data_str, timestamp, 'REMOVE')
        )
        
        # Delete the farm with the specified id
        cursor.execute('DELETE FROM locations WHERE id = ?', (farm_id,))
        
        conn.commit()
        conn.close()
        
        return f"Farm with id {farm_id} removed successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/edit_machine', methods=['GET', 'POST'])
def edit_machine():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        machine_id = get_param('id')
        
        if not machine_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the current machine data for logging
        cursor.execute('SELECT * FROM machines WHERE id = ?', (machine_id,))
        old_machine = cursor.fetchone()
        
        if old_machine is None:
            conn.close()
            return f"Error: No machine found with id {machine_id}", 404
        
        # Get column names
        columns = [description[0] for description in cursor.description]
        old_machine_data = dict(zip(columns, old_machine))
        
        # Get all possible fields that can be updated
        field_mappings = {
            'name': get_param('name'),
            'ip': get_param('ip'),
            'port': get_param('port'),
            'gearratio': get_param('gearratio'),
            'wheeldiameter_m': get_param('wheeldiameter_m'),
            'motorpoles': get_param('motorpoles'),
            'turnradius_m': get_param('turnradius_m'),
            'steeringramp': get_param('steeringramp'),
            'axisdistance_m': get_param('axisdistance_m'),
            'servocenter': get_param('servocenter'),
            'servorange': get_param('servorange'),
            'yawIMUgain': get_param('yawIMUgain'),
            'servoPgain': get_param('servoPgain'),
            'servoIgain': get_param('servoIgain'),
            'servoDgain': get_param('servoDgain'),
            'maxleft_degrees': get_param('maxleft_degrees'),
            'maxright_degrees': get_param('maxright_degrees'),
            'centervoltage_V': get_param('centervoltage_V'),
            'iVehicletype': get_param('iVehicletype') or get_param('vehicle_type_id')
        }
        
        # Build UPDATE query with only the fields that are provided
        updates = []
        update_values = []
        for field_name, field_value in field_mappings.items():
            if field_value is not None:
                updates.append(f"{field_name} = ?")
                update_values.append(field_value)
                # Update the machine data for logging
                old_machine_data[field_name] = field_value
        
        if not updates:
            conn.close()
            return "Error: No fields provided to update", 400
        
        # Add the machine_id to the values for the WHERE clause
        update_values.append(machine_id)
        
        # Execute the UPDATE
        query = f"UPDATE machines SET {', '.join(updates)} WHERE id = ?"
        cursor.execute(query, update_values)
        
        if cursor.rowcount == 0:
            conn.close()
            return f"Error: No machine found with id {machine_id}", 404
        
        # Log the edit to log_machines table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        machine_data_str = str(old_machine_data)
        
        cursor.execute(
            'INSERT INTO log_machines (machine_id, machine_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (machine_id, machine_data_str, timestamp, 'EDIT')
        )
        
        conn.commit()
        conn.close()
        
        return f"Machine with id {machine_id} updated successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/edit_farm', methods=['GET', 'POST'])
def edit_farm():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        farm_id = get_param('id')
        
        if not farm_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the current farm data for logging
        cursor.execute('SELECT * FROM locations WHERE id = ?', (farm_id,))
        old_farm = cursor.fetchone()
        
        if old_farm is None:
            conn.close()
            return f"Error: No farm found with id {farm_id}", 404
        
        # Get column names
        columns = [description[0] for description in cursor.description]
        old_farm_data = dict(zip(columns, old_farm))
        
        # Get all possible fields that can be updated
        field_mappings = {
            'name': get_param('name'),
            'description': get_param('description'),
            'latitude': get_param('latitude'),
            'longitude': get_param('longitude'),
            'area_ha': get_param('area_ha'),
            'created_at': get_param('created_at')
        }
        
        # Build UPDATE query with only the fields that are provided
        updates = []
        update_values = []
        for field_name, field_value in field_mappings.items():
            if field_value is not None:
                updates.append(f"{field_name} = ?")
                update_values.append(field_value)
                # Update the farm data for logging
                old_farm_data[field_name] = field_value
        
        if not updates:
            conn.close()
            return "Error: No fields provided to update", 400
        
        # Add the farm_id to the values for the WHERE clause
        update_values.append(farm_id)
        
        # Execute the UPDATE
        query = f"UPDATE locations SET {', '.join(updates)} WHERE id = ?"
        cursor.execute(query, update_values)
        
        if cursor.rowcount == 0:
            conn.close()
            return f"Error: No farm found with id {farm_id}", 404
        
        # Log the edit to log_farms table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        farm_data_str = str(old_farm_data)
        
        cursor.execute(
            'INSERT INTO log_farms (farm_id, farm_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (farm_id, farm_data_str, timestamp, 'EDIT')
        )
        
        conn.commit()
        conn.close()
        
        return f"Farm with id {farm_id} updated successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/all_fields', methods=['GET', 'POST'])
def all_fields():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        farm = get_param('farm')
        
        if not farm:
            return "Error: 'farm' parameter is required", 400
        
        # Connect to SQLite database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Query fields from the fields table filtered by location = farm
        cursor.execute('SELECT * FROM fields WHERE location = ?', (farm,))
        fields = cursor.fetchall()
        
        # Get column names from cursor description
        column_names = [description[0] for description in cursor.description]
        conn.close()
        
        # Create XML structure
        root = ET.Element('fields')
        for field in fields:
            field_elem = ET.SubElement(root, 'field')
            for i, column_name in enumerate(column_names):
                ET.SubElement(field_elem, column_name).text = str(field[i])
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        # Write to file
        with open('fields.xml', 'w') as f:
            f.write(xml_str)
        
        return Response(xml_str, mimetype='application/xml')
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/field', methods=['GET', 'POST'])
def field():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        field_id = get_param('id')
        
        if not field_id:
            return "Error: 'id' parameter is required", 400
        
        # Connect to SQLite database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Get the field with the specified id
        cursor.execute('SELECT storedinfile FROM fields WHERE id = ?', (field_id,))
        result = cursor.fetchone()
        conn.close()
        
        if result is None:
            return f"Error: No field found with id {field_id}", 404
        
        storedinfile = result[0]
        
        if not storedinfile:
            return f"Error: Field {field_id} has no storedinfile value", 400
        
        # Read the content of the file from the fields folder
        file_path = f"fields/{storedinfile}"
        
        try:
            with open(file_path, 'r') as f:
                file_content = f.read()
        except FileNotFoundError:
            return f"Error: File {storedinfile} not found in fields folder", 404
        except Exception as e:
            return f"Error reading file: {str(e)}", 500
        
        return Response(file_content, mimetype='text/plain')
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/log', methods=['GET', 'POST'])
def log():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        path = get_param('path')
        
        if not path:
            return "Error: 'path' parameter is required", 400
        
        # Validate that path is an integer
        try:
            path_int = int(path)
        except ValueError:
            return "Error: 'path' parameter must be an integer", 400
        
        # Connect to SQLite database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Query drivelogs table for entries with matching path
        cursor.execute('SELECT * FROM drivelogs WHERE path = ?', (path_int,))
        logs = cursor.fetchall()
        
        # Get column names from cursor description
        column_names = [description[0] for description in cursor.description]
        conn.close()
        
        # Create XML structure
        root = ET.Element('posts')
        for log_entry in logs:
            post_elem = ET.SubElement(root, 'post')
            for i, column_name in enumerate(column_names):
                ET.SubElement(post_elem, column_name).text = str(log_entry[i])
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        return Response(xml_str, mimetype='application/xml')
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/all_paths', methods=['GET', 'POST'])
def all_paths():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        field = get_param('field')
        
        if not field:
            return "Error: 'field' parameter is required", 400
        
        # Validate that field is an integer
        try:
            field_int = int(field)
        except ValueError:
            return "Error: 'field' parameter must be an integer", 400
        
        # Connect to SQLite database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Query paths from the paths table filtered by field
        cursor.execute('SELECT * FROM paths WHERE field = ?', (field_int,))
        paths = cursor.fetchall()
        
        # Get column names from cursor description
        column_names = [description[0] for description in cursor.description]
        conn.close()
        
        # Create XML structure
        root = ET.Element('paths')
        for path in paths:
            path_elem = ET.SubElement(root, 'path')
            for i, column_name in enumerate(column_names):
                ET.SubElement(path_elem, column_name).text = str(path[i])
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        # Write to file
        with open('paths.xml', 'w') as f:
            f.write(xml_str)
        
        return Response(xml_str, mimetype='application/xml')
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/add_path', methods=['POST'])
def add_path():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        # Get path fields - adjust these based on your paths table schema
        name = get_param('name')
        field = get_param('field')
        
        # Validate required fields
        if not name:
            return "Error: 'name' field is required", 400
        
        if not field:
            return "Error: 'field' field is required", 400
        
        # Validate that field is an integer
        try:
            field_int = int(field)
        except ValueError:
            return "Error: 'field' parameter must be an integer", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Build the INSERT query with all provided fields
        columns = ['name', 'field']
        values = [name, field_int]
        placeholders = ['?', '?']
        
        # Add optional fields if provided
        field_mappings = [
            ('description', get_param('description')),
            ('length_m', get_param('length_m')),
            ('width_m', get_param('width_m')),
            ('area_m2', get_param('area_m2')),
            ('created_at', get_param('created_at')),
        ]
        
        for field_name, field_value in field_mappings:
            if field_value is not None:
                columns.append(field_name)
                values.append(field_value)
                placeholders.append('?')
        
        # Create and execute the INSERT statement
        columns_str = ', '.join(columns)
        placeholders_str = ', '.join(placeholders)
        query = f'INSERT INTO paths ({columns_str}) VALUES ({placeholders_str})'
        cursor.execute(query, values)
        
        # Get the ID of the newly inserted path
        path_id = cursor.lastrowid
        
        # Log the addition to log_paths table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Build the path data as a string for logging
        path_data = {col: val for col, val in zip(columns, values)}
        path_data_str = str(path_data)
        
        cursor.execute(
            'INSERT INTO log_paths (path_id, path_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (path_id, path_data_str, timestamp, 'ADD')
        )
        
        conn.commit()
        conn.close()
        
        return "Path added successfully", 201
        
    except sqlite3.IntegrityError as e:
        return f"Error: {str(e)}", 409
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/add_unconnected_path', methods=['POST'])
def add_unconnected_path():
    """Add a path from an unconnected field file to the database."""
    try:
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        # Get required parameters
        field_id = get_param('field_id')
        path_name = get_param('path_name')
        path_filename = get_param('path_filename')
        
        # Validate required fields
        if not field_id:
            return "Error: 'field_id' parameter is required", 400
        
        if not path_name:
            return "Error: 'path_name' parameter is required", 400
        
        if not path_filename:
            return "Error: 'path_filename' parameter is required", 400
        
        # Validate that field_id is an integer
        try:
            field_id_int = int(field_id)
        except ValueError:
            return "Error: 'field_id' parameter must be an integer", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Insert the path into the database
        # iPath will auto-increment
        cursor.execute(
            'INSERT INTO paths (name, field, storedinfile) VALUES (?, ?, ?)',
            (path_name, field_id_int, path_filename)
        )
        
        # Get the ID of the newly inserted path
        path_id = cursor.lastrowid
        
        conn.commit()
        conn.close()
        
        return f"Path added successfully with ID: {path_id}", 201
        
    except sqlite3.IntegrityError as e:
        return f"Error: {str(e)}", 409
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/edit_path', methods=['GET', 'POST'])
def edit_path():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        path_id = get_param('id')
        
        if not path_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the current path data for logging
        cursor.execute('SELECT * FROM paths WHERE id = ?', (path_id,))
        old_path = cursor.fetchone()
        
        if old_path is None:
            conn.close()
            return f"Error: No path found with id {path_id}", 404
        
        # Get column names
        columns = [description[0] for description in cursor.description]
        old_path_data = dict(zip(columns, old_path))
        
        # Get all possible fields that can be updated
        field_mappings = {
            'name': get_param('name'),
            'field': get_param('field'),
            'description': get_param('description'),
            'length_m': get_param('length_m'),
            'width_m': get_param('width_m'),
            'area_m2': get_param('area_m2'),
            'created_at': get_param('created_at')
        }
        
        # Build UPDATE query with only the fields that are provided
        updates = []
        update_values = []
        for field_name, field_value in field_mappings.items():
            if field_value is not None:
                # Handle integer fields
                if field_name == 'field':
                    try:
                        field_value = int(field_value)
                    except ValueError:
                        return "Error: 'field' must be an integer", 400
                updates.append(f"{field_name} = ?")
                update_values.append(field_value)
                # Update the path data for logging
                old_path_data[field_name] = field_value
        
        if not updates:
            conn.close()
            return "Error: No fields provided to update", 400
        
        # Add the path_id to the values for the WHERE clause
        update_values.append(path_id)
        
        # Execute the UPDATE
        query = f"UPDATE paths SET {', '.join(updates)} WHERE id = ?"
        cursor.execute(query, update_values)
        
        if cursor.rowcount == 0:
            conn.close()
            return f"Error: No path found with id {path_id}", 404
        
        # Log the edit to log_paths table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        path_data_str = str(old_path_data)
        
        cursor.execute(
            'INSERT INTO log_paths (path_id, path_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (path_id, path_data_str, timestamp, 'EDIT')
        )
        
        conn.commit()
        conn.close()
        
        return f"Path with id {path_id} updated successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/read_path', methods=['GET', 'POST'])
def read_path():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value

        path_id = get_param('id')

        if not path_id:
            return "Error: 'id' field is required", 400

        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()

        # Get the path with the specified id
        cursor.execute('SELECT * FROM paths WHERE id = ?', (path_id,))
        path = cursor.fetchone()

        # Get column names
        column_names = [description[0] for description in cursor.description]
        conn.close()

        if path is None:
            return f"Error: No path found with id {path_id}", 404

        # Create XML structure
        root = ET.Element('paths')
        path_elem = ET.SubElement(root, 'path')
        for i, column_name in enumerate(column_names):
            ET.SubElement(path_elem, column_name).text = str(path[i])

        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')

        return Response(xml_str, mimetype='application/xml')

    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/remove_path', methods=['GET', 'POST'])
def remove_path():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        path_id = get_param('id')
        
        if not path_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the path data before deleting
        cursor.execute('SELECT * FROM paths WHERE id = ?', (path_id,))
        path = cursor.fetchone()
        
        if path is None:
            conn.close()
            return f"Error: No path found with id {path_id}", 404
        
        # Build path data string for logging
        from datetime import datetime
        columns = [description[0] for description in cursor.description]
        path_data = dict(zip(columns, path))
        path_data_str = str(path_data)
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Log the removal to log_paths table
        cursor.execute(
            'INSERT INTO log_paths (path_id, path_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (path_id, path_data_str, timestamp, 'REMOVE')
        )
        
        # Delete the path with the specified id
        cursor.execute('DELETE FROM paths WHERE id = ?', (path_id,))
        
        conn.commit()
        conn.close()
        
        return f"Path with id {path_id} removed successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/add_field', methods=['POST'])
def add_field():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        # Get field fields - adjust these based on your fields table schema
        name = get_param('name')
        
        # Validate required fields
        if not name:
            return "Error: 'name' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Build the INSERT query with all provided fields
        columns = ['name']
        values = [name]
        placeholders = ['?']
        
        # Add optional fields if provided
        field_mappings = [
            ('description', get_param('description')),
            ('farm_id', get_param('farm_id')),
            ('location_id', get_param('location_id')),
            ('area_ha', get_param('area_ha')),
            ('crop_type', get_param('crop_type')),
            ('soil_type', get_param('soil_type')),
            ('created_at', get_param('created_at')),
        ]
        
        for field_name, field_value in field_mappings:
            if field_value is not None:
                columns.append(field_name)
                values.append(field_value)
                placeholders.append('?')
        
        # Create and execute the INSERT statement
        columns_str = ', '.join(columns)
        placeholders_str = ', '.join(placeholders)
        query = f'INSERT INTO fields ({columns_str}) VALUES ({placeholders_str})'
        cursor.execute(query, values)
        
        # Get the ID of the newly inserted field
        field_id = cursor.lastrowid
        
        # Log the addition to log_fields table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Build the field data as a string for logging
        field_data = {col: val for col, val in zip(columns, values)}
        field_data_str = str(field_data)
        
        cursor.execute(
            'INSERT INTO log_fields (field_id, field_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (field_id, field_data_str, timestamp, 'ADD')
        )
        
        conn.commit()
        conn.close()
        
        return "Field added successfully", 201
        
    except sqlite3.IntegrityError as e:
        return f"Error: {str(e)}", 409
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/edit_field', methods=['GET', 'POST'])
def edit_field():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        field_id = get_param('id')
        
        if not field_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the current field data for logging
        cursor.execute('SELECT * FROM fields WHERE id = ?', (field_id,))
        old_field = cursor.fetchone()
        
        if old_field is None:
            conn.close()
            return f"Error: No field found with id {field_id}", 404
        
        # Get column names
        columns = [description[0] for description in cursor.description]
        old_field_data = dict(zip(columns, old_field))
        
        # Get all possible fields that can be updated
        field_mappings = {
            'name': get_param('name'),
            'description': get_param('description'),
            'farm_id': get_param('farm_id'),
            'location_id': get_param('location_id'),
            'area_ha': get_param('area_ha'),
            'crop_type': get_param('crop_type'),
            'soil_type': get_param('soil_type'),
            'created_at': get_param('created_at')
        }
        
        # Build UPDATE query with only the fields that are provided
        updates = []
        update_values = []
        for field_name, field_value in field_mappings.items():
            if field_value is not None:
                updates.append(f"{field_name} = ?")
                update_values.append(field_value)
                # Update the field data for logging
                old_field_data[field_name] = field_value
        
        if not updates:
            conn.close()
            return "Error: No fields provided to update", 400
        
        # Add the field_id to the values for the WHERE clause
        update_values.append(field_id)
        
        # Execute the UPDATE
        query = f"UPDATE fields SET {', '.join(updates)} WHERE id = ?"
        cursor.execute(query, update_values)
        
        if cursor.rowcount == 0:
            conn.close()
            return f"Error: No field found with id {field_id}", 404
        
        # Log the edit to log_fields table
        from datetime import datetime
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        field_data_str = str(old_field_data)
        
        cursor.execute(
            'INSERT INTO log_fields (field_id, field_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (field_id, field_data_str, timestamp, 'EDIT')
        )
        
        conn.commit()
        conn.close()
        
        return f"Field with id {field_id} updated successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/read_field', methods=['GET', 'POST'])
def read_field():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value

        field_id = get_param('id')

        if not field_id:
            return "Error: 'id' field is required", 400

        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()

        # Get the field with the specified id
        cursor.execute('SELECT * FROM fields WHERE id = ?', (field_id,))
        field = cursor.fetchone()

        # Get column names
        column_names = [description[0] for description in cursor.description]
        conn.close()

        if field is None:
            return f"Error: No field found with id {field_id}", 404

        # Create XML structure
        root = ET.Element('fields')
        field_elem = ET.SubElement(root, 'field')
        for i, column_name in enumerate(column_names):
            ET.SubElement(field_elem, column_name).text = str(field[i])

        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')

        return Response(xml_str, mimetype='application/xml')

    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/unconnected_paths')
def unconnected_paths():
    try:
        import os
        
        # Get all XML files in the paths folder
        paths_dir = 'paths'
        xml_files = []
        if os.path.exists(paths_dir) and os.path.isdir(paths_dir):
            for filename in os.listdir(paths_dir):
                if filename.lower().endswith('.xml'):
                    xml_files.append(filename)
        
        # Get all storedinfile values from the paths table
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        cursor.execute('SELECT storedinfile FROM paths WHERE storedinfile IS NOT NULL')
        db_files = [row[0] for row in cursor.fetchall()]
        conn.close()
        
        # Extract just the filename part from storedinfile (handle paths like 'norr\demo19nov.xml')
        db_filenames = []
        for db_file in db_files:
            if db_file:
                # Handle both forward and backward slashes
                filename = os.path.basename(db_file.replace('\\', '/'))
                db_filenames.append(filename)
        
        # Find XML files that are not referenced in the database
        unconnected = [f for f in xml_files if f not in db_filenames]
        
        # Create XML structure
        root = ET.Element('unconnected_paths')
        for filename in unconnected:
            path_elem = ET.SubElement(root, 'path')
            ET.SubElement(path_elem, 'filename').text = filename
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        return Response(xml_str, mimetype='application/xml')
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/unconnected_fields')
def unconnected_fields():
    try:
        import os
        
        # Get all XML files in the fields folder
        fields_dir = 'fields'
        xml_files = []
        if os.path.exists(fields_dir) and os.path.isdir(fields_dir):
            for filename in os.listdir(fields_dir):
                if filename.lower().endswith('.xml'):
                    xml_files.append(filename)
        
        # Get all storedinfile values from the fields table
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        cursor.execute('SELECT storedinfile FROM fields WHERE storedinfile IS NOT NULL')
        db_files = [row[0] for row in cursor.fetchall()]
        conn.close()
        
        # Find XML files that are not referenced in the database
        unconnected = [f for f in xml_files if f not in db_files]
        
        # Create XML structure
        root = ET.Element('unconnected_paths')
        for filename in unconnected:
            path_elem = ET.SubElement(root, 'path')
            ET.SubElement(path_elem, 'filename').text = filename
        
        # Convert to XML string with declaration
        xml_str = '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode')
        
        return Response(xml_str, mimetype='application/xml')
        
    except Exception as e:
        return f"Error: {str(e)}", 500


@app.route('/remove_field', methods=['GET', 'POST'])
def remove_field():
    try:
        # Get all the fields from POST form data or URL parameters
        def get_param(name):
            value = request.form.get(name) or request.args.get(name)
            return value
        
        field_id = get_param('id')
        
        if not field_id:
            return "Error: 'id' field is required", 400
        
        # Connect to database
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # First, get the field data before deleting
        cursor.execute('SELECT * FROM fields WHERE id = ?', (field_id,))
        field = cursor.fetchone()
        
        if field is None:
            conn.close()
            return f"Error: No field found with id {field_id}", 404
        
        # Build field data string for logging
        from datetime import datetime
        columns = [description[0] for description in cursor.description]
        field_data = dict(zip(columns, field))
        field_data_str = str(field_data)
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # Log the removal to log_fields table
        cursor.execute(
            'INSERT INTO log_fields (field_id, field_data, timestamp, action) VALUES (?, ?, ?, ?)',
            (field_id, field_data_str, timestamp, 'REMOVE')
        )
        
        # Delete the field with the specified id
        cursor.execute('DELETE FROM fields WHERE id = ?', (field_id,))
        
        conn.commit()
        conn.close()
        
        return f"Field with id {field_id} removed successfully", 200
        
    except Exception as e:
        return f"Error: {str(e)}", 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080, debug=True)
