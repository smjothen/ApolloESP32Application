table_service = TableService(
    account_name='zapcloudprod',
    sas_token='st=2020-06-16T06%3A56%3A00Z&se=2022-06-19T06%3A56%3A00Z&sp=r&spk=ZAP000000&epk=ZAP000003&sv=2018-03-28&tn=observations&sig=fmHhaEQw77P%2FZNKfp5Pv8Q1XYPwy%2BZiE%2BjXWx08P8RE%3D'
)
task = table_service.get_entity('Observations', 'ZAP000001', '808_9005605823888721')                                                                                                      
task.value                                                                                                                                                                                
# '[MEMORY USE] (GetFreeHeapSize now: 204728, GetMinimumEverFreeHeapSize: 188672, heap_caps_get_free_size: 287688)'
