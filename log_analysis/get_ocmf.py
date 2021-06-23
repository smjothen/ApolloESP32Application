
from azure.cosmosdb.table.tableservice import TableService
from azure.cosmosdb.table.models import Entity
import json
from pprint import pprint

table_service = TableService(
    account_name='zapcloudprod',
    sas_token='st=2021-02-08T12%3A44%3A31Z&se=2022-02-09T12%3A44%3A00Z&sp=r&sv=2018-03-28&tn=observations&sig=Qr1tihbC9BP%2FLGBBHx6YM6YnjApoRnGrWtnYYAGq39o%3D'
)


ocmf_observations = table_service.query_entities(
    'Observations',
    filter="PartitionKey eq 'ZAP000301' and RowKey lt '555_' and RowKey ge '554_' "
)

ocmf_observations = list(ocmf_observations)

print(f"got {len(ocmf_observations)} observations")

parsed_ocmf = []

for e in ocmf_observations:
    ocmf_text = e.value[5:].strip()
    ocmf = json.loads(ocmf_text)
    parsed_ocmf.append(ocmf)

#parsed_ocmf.sort(key=lambda e: e['RD'][0]['TM'])
parsed_ocmf.reverse()

prev = parsed_ocmf

for e, prev_e in zip(parsed_ocmf[1:], prev):
    energy = e['RD'][0]['RV']
    delta = energy - prev_e['RD'][0]['RV']
    print(f"E: {energy:15.15} \tchange:{delta:15.15} \t@ {e['RD'][0]['TM']}")
    
