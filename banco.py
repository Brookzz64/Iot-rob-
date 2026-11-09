import datetime
import math
import sqlite3
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, field_validator

app = FastAPI(title="API Telemetria - Teste Local com SQLite")
DB_FILE = "telemetria_teste.db"

# ==============================================================================
# INICIALIZAÇÃO DO BANCO DE DADOS
# ==============================================================================
def init_db():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS leituras (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            nivel_tinta REAL,
            temperatura REAL,
            humidade REAL,
            luminosidade INTEGER,
            presenca INTEGER
        )
    ''')
    conn.commit()
    conn.close()

# Cria a tabela automaticamente se ela ainda não existir
init_db()

# ==============================================================================
# MODELO DE DADOS ENVIADO PELO ESP32
# ==============================================================================
class Telemetria(BaseModel):
    nivel_tinta: float
    temperatura: float
    humidade: float
    luminosidade: int
    presenca: int

    # Rejeita valores invalidos (nan/inf) que um sensor com defeito pode enviar.
    @field_validator("nivel_tinta", "temperatura", "humidade")
    @classmethod
    def validar_float_finito(cls, v: float) -> float:
        if math.isnan(v) or math.isinf(v):
            raise ValueError("valor float invalido (nan/inf)")
        return v

# ==============================================================================
# ROTAS DA API
# ==============================================================================
@app.post("/api/telemetria")
async def receber_telemetria(dados: Telemetria):
    try:
        # 'with' garante commit/rollback e fechamento da conexao mesmo em erro.
        with sqlite3.connect(DB_FILE) as conn:
            conn.execute('''
                INSERT INTO leituras (nivel_tinta, temperatura, humidade, luminosidade, presenca)
                VALUES (?, ?, ?, ?, ?)
            ''', (dados.nivel_tinta, dados.temperatura, dados.humidade, dados.luminosidade, dados.presenca))
            conn.commit()

        hora_atual = datetime.datetime.now().strftime('%H:%M:%S')
        print(f"[{hora_atual}] [SQLITE OK] Dados salvos: {dados.model_dump()}")
        return {"status": "sucesso", "mensagem": "Dado salvo no SQLite!"}

    except Exception as e:
        print(f"[ERRO] Falha ao salvar no SQLite: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/telemetria")
async def listar_ultimas_leituras():
    """ Rota para verificar no navegador os últimos 10 dados gravados """
    with sqlite3.connect(DB_FILE) as conn:
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM leituras ORDER BY id DESC LIMIT 10")
        colunas = [desc[0] for desc in cursor.description]
        linhas = cursor.fetchall()

    resultado = [dict(zip(colunas, linha)) for linha in linhas]
    return {"total": len(resultado), "dados": resultado}


# ==============================================================================
# EXECUCAO DO SERVIDOR
# ==============================================================================
# host="0.0.0.0" e OBRIGATORIO para o ESP32 alcancar a API pela rede.
# Com "127.0.0.1" so funcionaria no proprio PC.
if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
