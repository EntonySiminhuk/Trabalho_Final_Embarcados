# VibraMonitor — Plataforma de Monitoramento de Vibração

Recebe os dados de vibração do sensor (STM32 + MPU6050), armazena no PostgreSQL
e exibe tudo num dashboard em tempo real.

```
                          ┌──────────────────────────────┐
  ESP32 ──HTTP POST────►  │  API .NET 10  ──►  PostgreSQL │
  (ou simulador)          │       │ SignalR              │
                          └───────┼──────────────────────┘
                                  ▼
                          Dashboard React (tempo real)
```

## Componentes

| Camada     | Tecnologia                        | Pasta                 |
|------------|-----------------------------------|-----------------------|
| Banco      | PostgreSQL 16 (Docker)            | `docker-compose.yml`  |
| Backend    | ASP.NET Core 10 + EF Core + SignalR | `backend/`          |
| Frontend   | React + Vite + TypeScript + Recharts | `frontend/`        |

## Pré-requisitos

- .NET SDK 10
- Node.js 20+
- Docker Desktop

## Como rodar (fase isolada — com simulador)

Abra **3 terminais**:

### 1. Banco de dados
```powershell
cd webapp
docker compose up -d postgres
```
> Postgres sobe na porta **5544** do host (evita conflito com um Postgres nativo na 5432).
> Opcional: `docker compose --profile tools up -d pgadmin` → pgAdmin em http://localhost:8081

### 2. Backend (API)
```powershell
cd webapp/backend/VibraMonitor.Api
dotnet run
```
- API: http://localhost:5080
- Documentação interativa (Scalar): http://localhost:5080/scalar
- O **simulador** já vem ligado (`Simulator:Enabled=true` no `appsettings.json`) e
  gera dados a cada 1 s no device `SIM-01`. Para desligar quando o hardware estiver
  conectado, mude para `false`.

### 3. Frontend (dashboard)
```powershell
cd webapp/frontend
npm install   # só na primeira vez
npm run dev
```
- Dashboard: http://localhost:5173

## Endpoints da API

| Método | Rota                     | Descrição                              |
|--------|--------------------------|----------------------------------------|
| POST   | `/api/readings`          | Ingere uma leitura (usado pelo ESP32)  |
| POST   | `/api/readings/batch`    | Ingere várias leituras                 |
| GET    | `/api/readings`          | Série temporal (filtros: deviceId, from, to, limit) |
| GET    | `/api/readings/latest`   | Última leitura                         |
| GET    | `/api/stats`             | Estatísticas agregadas                 |
| GET    | `/api/devices`           | Lista de dispositivos                  |
| GET    | `/health`                | Healthcheck                            |
| WS     | `/hubs/vibration`        | SignalR — evento `reading` em tempo real |

### Exemplo de ingestão
```bash
curl -X POST http://localhost:5080/api/readings \
  -H "Content-Type: application/json" \
  -d '{"deviceId":"STM32-01","harmonicDc":510,"harmonic1st":145.5,"harmonic2nd":40,"harmonic3rd":12,"harmonic4th":6}'
```
`harmonic1st > 100` → a API marca `isAnomaly = true` (mesmo limite do firmware).

## Configuração

`backend/VibraMonitor.Api/appsettings.json`:
- `ConnectionStrings:Postgres` — conexão com o banco
- `Monitoring:AnomalyThreshold` — limite crítico da 1ª harmônica (padrão 100, igual ao firmware)
- `Simulator:Enabled` / `IntervalMs` / `DeviceId` — controle do simulador

`frontend/.env`:
- `VITE_API_URL` — URL da API (padrão `http://localhost:5080`)

## Fase 2 — integração com o hardware

Ver `../esp32_bridge/` e a seção de integração no README raiz do projeto.
Em resumo: o ESP32 lê as harmônicas do STM32 pela UART e faz `POST /api/readings`.
Ao ativar o hardware, desligue o simulador (`Simulator:Enabled=false`).
