# Time Sync Scenarios

The diagrams below detail the current plan for synchronization of different clocks.

__TODO__: Do we need to set the time over OCPP?


## on boot

```mermaid
sequenceDiagram
    ext. RTC->>ESP: set rtc

    alt offline
        loop re-sync
        ext. RTC->>ESP: set rtc
      end
    else online
        loop re-sync
            NTP->>ESP: set rtc
            ESP->>ext. RTC: pass set rtc
        end
    end
```
[![](https://mermaid.ink/img/eyJjb2RlIjoic2VxdWVuY2VEaWFncmFtXG4gICAgZXh0LiBSVEMtPj5FU1A6IHNldCBydGNcblxuICAgIGFsdCBvZmZsaW5lXG4gICAgICAgIGxvb3AgcmUtc3luY1xuICAgICAgICBleHQuIFJUQy0-PkVTUDogc2V0IHJ0Y1xuICAgICAgZW5kXG4gICAgZWxzZSBvbmxpbmVcbiAgICAgICAgbG9vcCByZS1zeW5jXG4gICAgICAgICAgICBOVFAtPj5FU1A6IHNldCBydGNcbiAgICAgICAgICAgIEVTUC0-PmV4dC4gUlRDOiBwYXNzIHNldCBydGNcbiAgICAgICAgZW5kXG4gICAgZW5kIiwibWVybWFpZCI6eyJ0aGVtZSI6ImRlZmF1bHQifSwidXBkYXRlRWRpdG9yIjpmYWxzZX0)](https://mermaid-js.github.io/mermaid-live-editor/#/edit/eyJjb2RlIjoic2VxdWVuY2VEaWFncmFtXG4gICAgZXh0LiBSVEMtPj5FU1A6IHNldCBydGNcblxuICAgIGFsdCBvZmZsaW5lXG4gICAgICAgIGxvb3AgcmUtc3luY1xuICAgICAgICBleHQuIFJUQy0-PkVTUDogc2V0IHJ0Y1xuICAgICAgZW5kXG4gICAgZWxzZSBvbmxpbmVcbiAgICAgICAgbG9vcCByZS1zeW5jXG4gICAgICAgICAgICBOVFAtPj5FU1A6IHNldCBydGNcbiAgICAgICAgICAgIEVTUC0-PmV4dC4gUlRDOiBwYXNzIHNldCBydGNcbiAgICAgICAgZW5kXG4gICAgZW5kIiwibWVybWFpZCI6eyJ0aGVtZSI6ImRlZmF1bHQifSwidXBkYXRlRWRpdG9yIjpmYWxzZX0)

## Initial setup
```mermaid
sequenceDiagram
    alt offline
        phone->>ESP: set time
        ESP->>ext. RTC: pass set rtc
    else online
        loop re-sync
            NTP->>ESP: set rtc
            ESP->>ext. RTC: pass set rtc
        end
    end
```
[![](https://mermaid.ink/img/eyJjb2RlIjoic2VxdWVuY2VEaWFncmFtXG4gICAgYWx0IG9mZmxpbmVcbiAgICAgICAgcGhvbmUtPj5FU1A6IHNldCB0aW1lXG4gICAgICAgIEVTUC0-PmV4dC4gUlRDOiBwYXNzIHNldCBydGNcbiAgICBlbHNlIG9ubGluZVxuICAgICAgICBsb29wIHJlLXN5bmNcbiAgICAgICAgICAgIE5UUC0-PkVTUDogc2V0IHJ0Y1xuICAgICAgICAgICAgRVNQLT4-ZXh0LiBSVEM6IHBhc3Mgc2V0IHJ0Y1xuICAgICAgICBlbmRcbiAgICBlbmRcbiIsIm1lcm1haWQiOnsidGhlbWUiOiJkZWZhdWx0In19)](https://mermaid-js.github.io/mermaid-live-editor/#/edit/eyJjb2RlIjoic2VxdWVuY2VEaWFncmFtXG4gICAgYWx0IG9mZmxpbmVcbiAgICAgICAgcGhvbmUtPj5FU1A6IHNldCB0aW1lXG4gICAgICAgIEVTUC0-PmV4dC4gUlRDOiBwYXNzIHNldCBydGNcbiAgICBlbHNlIG9ubGluZVxuICAgICAgICBsb29wIHJlLXN5bmNcbiAgICAgICAgICAgIE5UUC0-PkVTUDogc2V0IHJ0Y1xuICAgICAgICAgICAgRVNQLT4-ZXh0LiBSVEM6IHBhc3Mgc2V0IHJ0Y1xuICAgICAgICBlbmRcbiAgICBlbmRcbiIsIm1lcm1haWQiOnsidGhlbWUiOiJkZWZhdWx0In19)