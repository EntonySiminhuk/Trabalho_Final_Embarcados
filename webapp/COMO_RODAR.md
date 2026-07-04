# Como rodar o projeto (passo a passo)

Guia rápido para subir os três componentes: **banco de dados**, **backend** e **frontend**.

---

## 0. Pré-requisitos (instalar uma vez)

| Ferramenta       | Versão   | Como conferir            |
|------------------|----------|--------------------------|
| Docker Desktop   | qualquer | `docker --version`       |
| .NET SDK         | 10+      | `dotnet --version`       |
| Node.js          | 20+      | `node --version`         |

> **Docker Desktop precisa estar aberto e rodando** antes do passo 1
> (o ícone da baleia na bandeja do Windows tem que estar "verde").

---

## Ordem de inicialização

Suba **nesta ordem**: banco → backend → frontend.
Abra **3 terminais** (PowerShell), um para cada componente.

---

## 1️⃣ Banco de dados (PostgreSQL)

```powershell
cd webapp
docker compose up -d postgres
```

- O banco sobe na porta **5544** do seu PC (não é a 5432 — foi escolhida a 5544
  para não conflitar com um PostgreSQL já instalado nesta máquina).
- Roda em segundo plano (`-d`), não precisa deixar o terminal aberto.

**Conferir se está no ar:**
```powershell
docker ps
```
Deve aparecer o container `vibra_postgres` com status `healthy`.

**(Opcional) Interface visual do banco (pgAdmin):**
```powershell
docker compose --profile tools up -d pgadmin
```
Acesse http://localhost:8081 — login `admin@vibra.local` / senha `admin`.

---

## 2️⃣ Backend (API .NET)

```powershell
cd webapp/backend/VibraMonitor.Api
dotnet run
```

- Deixe **este terminal aberto** (a API fica rodando aqui).
- Na primeira execução ele baixa pacotes e cria as tabelas no banco automaticamente.
- Quando aparecer `Now listening on: http://localhost:5080`, está pronto.

**Endereços:**
- API: http://localhost:5080
- Healthcheck: http://localhost:5080/health
- Documentação interativa da API (Scalar): http://localhost:5080/scalar

> Por padrão o **simulador** já vem ligado e gera dados de teste a cada 1 segundo.
> Para desligar (ex.: quando o hardware real estiver conectado), edite
> `appsettings.json` → `"Simulator": { "Enabled": false }` e rode `dotnet run` de novo.

---

## 3️⃣ Frontend (Dashboard React)

```powershell
cd webapp/frontend
npm install    # só na PRIMEIRA vez (baixa as dependências)
npm run dev
```

- Deixe **este terminal aberto** (o dashboard fica rodando aqui).
- Abra no navegador: **http://localhost:5173**

Se tudo estiver certo, o dashboard mostra os KPIs, os gráficos se atualizando
em tempo real e o indicador **"tempo real"** verde no canto superior direito.

---

## Como parar tudo

| Componente | Como parar                                             |
|------------|--------------------------------------------------------|
| Frontend   | No terminal dele, aperte `Ctrl + C`                    |
| Backend    | No terminal dele, aperte `Ctrl + C`                    |
| Banco      | `cd webapp` e depois `docker compose down`             |

> `docker compose down` para o banco mas **mantém os dados** (ficam no volume).
> Para apagar os dados também: `docker compose down -v`.

---

## Resumo rápido (copiar e colar)

```powershell
# Terminal 1 — banco
cd webapp
docker compose up -d postgres

# Terminal 2 — backend
cd webapp/backend/VibraMonitor.Api
dotnet run

# Terminal 3 — frontend
cd webapp/frontend
npm install
npm run dev
```
Depois abra **http://localhost:5173**.

---

## Problemas comuns

| Sintoma | Causa provável | Solução |
|---------|----------------|---------|
| `error during connect... docker daemon` | Docker Desktop não está aberto | Abra o Docker Desktop e espere ficar "verde", depois repita o passo 1 |
| API cai com `28P01: password authentication failed` | O banco não subiu, ou algo mais está na porta 5544 | Confira `docker ps`; garanta que o `vibra_postgres` está `healthy` |
| Dashboard abre mas não mostra dados / fica "offline" | Backend não está rodando | Verifique o terminal 2; a API tem que estar em `http://localhost:5080` |
| `npm run dev` falha na primeira vez | Dependências não instaladas | Rode `npm install` antes |
| Porta 5173 ou 5080 "em uso" | Uma execução anterior ficou aberta | Feche o terminal antigo ou o processo que está usando a porta |
