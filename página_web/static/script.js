const charts = {}
let dateRange
let socket
const latestData = { dht11: {}, hw080: {}, stress: {} }
let activeNodes = [1, 2, 3, 4]

const nodeInfo = {
  1: { name: "Jordan Manguay", mac: "08:A6:F7:BC:F7:F1", ipv6: "2001:db8::aa6:f7ff:febc:f7f1" },
  2: { name: "Ariel Gonzales", mac: "FC:B4:67:F1:3C:4D", ipv6: "2001:db8::feb4:67ff:fef1:3c4d" },
  3: { name: "Cristopher Ruales", mac: "10:06:1C:87:85:C1", ipv6: "2001:db8::1206:1cff:fe87:85c1" },
  4: { name: "Anthony Ibujes", mac: "6E:B2:4E:8D:76:1F", ipv6: "2001:db8::7cf3:5e25:c045:65af" },
  router: { name: "Router de Borde", mac: "6E:07:A3:82:E3:72", ipv6: "2001:db8:a::2" },
}

let actuatorState = "off"
let actuatorScheduled = false

document.addEventListener("DOMContentLoaded", () => {
  dateRange = flatpickr("#date-range", {
    mode: "range",
    dateFormat: "Y-m-d H:i",
    defaultDate: [new Date(Date.now() - 24 * 60 * 60 * 1000), new Date()],
    enableTime: true,
  })

  document.getElementById("update-btn").addEventListener("click", updateCharts)
  document.getElementById("show-last-n").addEventListener("change", toggleRealTimeMode)
  document.getElementById("n-values").addEventListener("input", updateLastNValues)

  document.getElementById("toggle-node-info").addEventListener("click", toggleNodeInfoTable)

  document.getElementById("actuator-toggle").addEventListener("change", toggleActuator)
  document.getElementById("set-actuator").addEventListener("click", setActuator)
  document.getElementById("toggle-schedule").addEventListener("click", toggleSchedule)

  initSocket()
  fetchInitialData()
  initNodeMap()
  createNodeFilter()

  // Start checking actuators every second
  setInterval(checkActuators, 1000)

  // Initialize the router color and actuator status
  updateRouterColor()
  updateActuatorStatus()

  // Load actuator schedule from localStorage
  loadActuatorSchedule()

  // Inicializar el estado del botón de programación
  updateToggleScheduleButton()
})

function createNodeFilter() {
  const filterContainer = document.getElementById("node-filter")
  for (let i = 1; i <= 4; i++) {
    const label = document.createElement("label")
    label.innerHTML = `
      <input type="checkbox" value="${i}" checked>
      <span class="checkmark"></span>
      Nodo ${i}
    `
    label.addEventListener("change", (e) => {
      if (e.target.checked) {
        activeNodes.push(Number.parseInt(e.target.value))
      } else {
        activeNodes = activeNodes.filter((node) => node !== Number.parseInt(e.target.value))
      }
      updateCharts()
    })
    filterContainer.appendChild(label)
  }
}

function initSocket() {
  socket = io()
  socket.on("new_data", (data) => {
    updateDataList([data])
    updateChartsWithNewData(data)
    updateNodeMap(data)
    updateNodeInfo(data)
  })
}

function toggleRealTimeMode() {
  const showLastN = document.getElementById("show-last-n").checked
  const datePickerContainer = document.getElementById("date-picker-container")
  datePickerContainer.style.display = showLastN ? "none" : "flex"

  if (showLastN) {
    startRealTimeUpdates()
  } else {
    stopRealTimeUpdates()
  }
}

let realTimeInterval

function startRealTimeUpdates() {
  const n = document.getElementById("n-values").value
  fetchLastNValues(n)
  realTimeInterval = setInterval(() => {
    fetchLastNValues(n)
  }, 5000) // Actualizar cada 5 segundos
}

function stopRealTimeUpdates() {
  if (realTimeInterval) {
    clearInterval(realTimeInterval)
  }
}

function fetchInitialData() {
  const [start, end] = dateRange.selectedDates
  fetchData(start, end)
}

function fetchData(start, end) {
  const url = `/api/data?start_date=${start.toISOString()}&end_date=${end.toISOString()}`

  fetch(url)
    .then((response) => response.json())
    .then((data) => {
      const processedData = processData(data)
      updateDHT11Charts(processedData.dht11)
      updateHW080Charts(processedData.hw080)
      updateStressChart(processedData.stress)
      updateDataList(data)
      updateNodeMap(data)
      updateNodeInfo(data)
    })
}

function fetchLastNValues(n) {
  const url = `/api/last_n_values?n=${n}`

  fetch(url)
    .then((response) => response.json())
    .then((data) => {
      const processedData = processData(data)
      updateDHT11Charts(processedData.dht11)
      updateHW080Charts(processedData.hw080)
      updateStressChart(processedData.stress)
      updateDataList(data)
      updateNodeMap(data)
      updateNodeInfo(data)
    })
}

function updateCharts() {
  const showLastN = document.getElementById("show-last-n").checked
  if (showLastN) {
    const n = document.getElementById("n-values").value
    fetchLastNValues(n)
  } else {
    fetchInitialData()
  }
}

function updateLastNValues() {
  if (document.getElementById("show-last-n").checked) {
    const n = document.getElementById("n-values").value
    fetchLastNValues(n)
  }
}

function updateChartsWithNewData(newData) {
  if (newData.sensor_type === "dht11") {
    if (!latestData.dht11[newData.node_number]) {
      latestData.dht11[newData.node_number] = []
    }
    latestData.dht11[newData.node_number].push(newData)
    updateDHT11Charts(latestData.dht11)
  } else if (newData.sensor_type === "hw080") {
    if (!latestData.hw080[newData.node_number]) {
      latestData.hw080[newData.node_number] = []
    }
    latestData.hw080[newData.node_number].push(newData)
    updateHW080Charts(latestData.hw080)
  }
  // Add stress data update
  if (!latestData.stress[newData.node_number]) {
    latestData.stress[newData.node_number] = []
  }
  latestData.stress[newData.node_number].push(newData)
  updateStressChart(latestData.stress)
  updateNodeMap(newData)
  updateNodeInfo(newData)
}

function processData(data) {
  const processedData = { dht11: {}, hw080: {}, stress: {} }
  const seenTimestamps = new Set()

  data.forEach((item) => {
    const key = `${item.node_number}-${item.timestamp}`
    if (!seenTimestamps.has(key)) {
      seenTimestamps.add(key)
      if (item.sensor_type === "dht11") {
        if (!processedData.dht11[item.node_number]) {
          processedData.dht11[item.node_number] = []
        }
        processedData.dht11[item.node_number].push(item)
      } else if (item.sensor_type === "hw080") {
        if (!processedData.hw080[item.node_number]) {
          processedData.hw080[item.node_number] = []
        }
        processedData.hw080[item.node_number].push(item)
      }

      // Añadir datos de estrés
      if (!processedData.stress[item.node_number]) {
        processedData.stress[item.node_number] = []
      }
      processedData.stress[item.node_number].push(item)
    }
  })
  return processedData
}

function formatTimestamp(timestamp) {
  const date = new Date(timestamp)
  return date.toLocaleString("es-ES", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
    timeZone: "UTC",
  })
}

function updateDHT11Charts(data) {
  const container = document.getElementById("dht11-charts")
  container.innerHTML = ""

  Object.entries(data)
    .filter(([nodeNumber]) => activeNodes.includes(Number.parseInt(nodeNumber)))
    .forEach(([nodeNumber, nodeData]) => {
      if (nodeData.length === 0) return
      const nodeContainer = document.createElement("div")
      nodeContainer.className = "node-container dht11-node"
      nodeContainer.innerHTML = `
                <h3>Nodo ${nodeNumber} - ${nodeData[0].name}</h3>
                <div class="chart-container">
                    <canvas id="dht11-temp-${nodeNumber}"></canvas>
                </div>
                <div class="chart-container">
                    <canvas id="dht11-humidity-${nodeNumber}"></canvas>
                </div>
            `
      container.appendChild(nodeContainer)

      createOrUpdateChart(`dht11-temp-${nodeNumber}`, createTemperatureChart, { node: nodeNumber, data: nodeData })
      createOrUpdateChart(`dht11-humidity-${nodeNumber}`, createHumidityChart, { node: nodeNumber, data: nodeData })
    })
}

function updateHW080Charts(data) {
  const container = document.getElementById("hw080-charts")
  container.innerHTML = ""

  Object.entries(data)
    .filter(([nodeNumber]) => activeNodes.includes(Number.parseInt(nodeNumber)))
    .forEach(([nodeNumber, nodeData]) => {
      if (nodeData.length === 0) return
      const nodeContainer = document.createElement("div")
      nodeContainer.className = "node-container hw080-node"
      nodeContainer.innerHTML = `
                <h3>Nodo ${nodeNumber} - ${nodeData[0].name}</h3>
                <div class="chart-container">
                    <canvas id="hw080-moisture-${nodeNumber}"></canvas>
                </div>
            `
      container.appendChild(nodeContainer)

      createOrUpdateChart(`hw080-moisture-${nodeNumber}`, createMoistureChart, { node: nodeNumber, data: nodeData })
    })
}

function createOrUpdateChart(chartId, createFunction, data) {
  if (charts[chartId]) {
    charts[chartId].destroy()
  }
  charts[chartId] = createFunction(data)
}

function createTemperatureChart({ node, data }) {
  const ctx = document.getElementById(`dht11-temp-${node}`).getContext("2d")
  const temperatures = data.map((item) => Number.parseFloat(item.temperature))
  const timestamps = data.map((item) => formatTimestamp(item.timestamp))

  return new Chart(ctx, {
    type: "line",
    data: {
      labels: timestamps,
      datasets: [
        {
          label: `Temperatura (°C)`,
          data: temperatures,
          borderColor: "rgba(255, 99, 132, 1)",
          backgroundColor: "rgba(255, 99, 132, 0.2)",
          tension: 0.3,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false },
        title: {
          display: true,
          text: `Temperatura - Nodo ${node} (${data[0].name})`,
          font: {
            size: 16,
            weight: "bold",
          },
        },
      },
      scales: {
        y: {
          beginAtZero: false,
          ticks: {
            font: {
              size: 12,
            },
          },
        },
        x: {
          ticks: {
            maxRotation: 0,
            autoSkip: true,
            maxTicksLimit: 8,
            font: {
              size: 12,
            },
          },
        },
      },
      elements: {
        line: {
          tension: 0.3,
        },
        point: {
          radius: 3,
        },
      },
    },
  })
}

function createHumidityChart({ node, data }) {
  const ctx = document.getElementById(`dht11-humidity-${node}`).getContext("2d")
  const humidity = data.map((item) => Number.parseFloat(item.humidity))
  const timestamps = data.map((item) => formatTimestamp(item.timestamp))

  return new Chart(ctx, {
    type: "line",
    data: {
      labels: timestamps,
      datasets: [
        {
          label: `Humedad (%)`,
          data: humidity,
          borderColor: "rgba(54, 162, 235, 1)",
          backgroundColor: "rgba(54, 162, 235, 0.2)",
          tension: 0.3,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false },
        title: {
          display: true,
          text: `Humedad - Nodo ${node} (${data[0].name})`,
          font: {
            size: 16,
            weight: "bold",
          },
        },
      },
      scales: {
        y: {
          beginAtZero: false,
          ticks: {
            font: {
              size: 12,
            },
          },
        },
        x: {
          ticks: {
            maxRotation: 0,
            autoSkip: true,
            maxTicksLimit: 8,
            font: {
              size: 12,
            },
          },
        },
      },
      elements: {
        line: {
          tension: 0.3,
        },
        point: {
          radius: 3,
        },
      },
    },
  })
}

function createMoistureChart({ node, data }) {
  const ctx = document.getElementById(`hw080-moisture-${node}`).getContext("2d")
  const moisture = data.map((item) => Number.parseFloat(item.moisture))
  const timestamps = data.map((item) => formatTimestamp(item.timestamp))

  return new Chart(ctx, {
    type: "line",
    data: {
      labels: timestamps,
      datasets: [
        {
          label: `Humedad del Suelo (%)`,
          data: moisture,
          borderColor: "rgba(75, 192, 192, 1)",
          backgroundColor: "rgba(75, 192, 192, 0.2)",
          tension: 0.3,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false },
        title: {
          display: true,
          text: `Humedad del Suelo - Nodo ${node} (${data[0].name})`,
          font: {
            size: 16,
            weight: "bold",
          },
        },
      },
      scales: {
        y: {
          beginAtZero: false,
          ticks: {
            font: {
              size: 12,
            },
          },
        },
        x: {
          ticks: {
            maxRotation: 0,
            autoSkip: true,
            maxTicksLimit: 8,
            font: {
              size: 12,
            },
          },
        },
      },
      elements: {
        line: {
          tension: 0.3,
        },
        point: {
          radius: 3,
        },
      },
    },
  })
}

function updateDataList(data) {
  const dataList = document.getElementById("data-list")
  data.slice(0, 10).forEach((item) => {
    const li = document.createElement("li")
    const formattedTime = formatTimestamp(item.timestamp)
    if (item.sensor_type === "dht11") {
      li.textContent = `${item.name} (Nodo ${item.node_number}): Temp: ${item.temperature}°C, Humedad: ${item.humidity}%, Tiempo: ${formattedTime}`
    } else if (item.sensor_type === "hw080") {
      li.textContent = `${item.name} (Nodo ${item.node_number}): Humedad del Suelo: ${item.moisture}%, Tiempo: ${formattedTime}`
    }
    dataList.insertBefore(li, dataList.firstChild)
    if (dataList.children.length > 10) {
      dataList.removeChild(dataList.lastChild)
    }
  })
}

function updateStressChart(data) {
  const stressData = calculateStressData(data)
  const ctx = document.getElementById("stress-pie-chart").getContext("2d")

  if (charts["stress-pie-chart"]) {
    charts["stress-pie-chart"].destroy()
  }

  charts["stress-pie-chart"] = new Chart(ctx, {
    type: "pie",
    data: {
      labels: ["Sin estrés", "Con estrés"],
      datasets: [
        {
          data: [stressData.withoutStress, stressData.withStress],
          backgroundColor: ["#2ecc71", "#e74c3c"],
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          position: "bottom",
        },
        title: {
          display: true,
          text: "Estado de Estrés Hídrico",
          font: {
            size: 16,
            weight: "bold",
          },
        },
      },
    },
  })
}

function calculateStressData(data) {
  let withStress = 0
  let withoutStress = 0

  Object.entries(data)
    .filter(([nodeNumber]) => activeNodes.includes(Number.parseInt(nodeNumber)))
    .forEach(([_, nodeData]) => {
      nodeData.forEach((item) => {
        if (item.estado_estres === 0) {
          withStress++
        } else if (item.estado_estres === 1) {
          withoutStress++
        }
      })
    })

  return { withStress, withoutStress }
}

function initNodeMap() {
  const nodes = document.querySelectorAll(".node")
  nodes.forEach((node) => {
    node.addEventListener("mouseenter", showTooltip)
    node.addEventListener("mouseleave", hideTooltip)
  })
}

function updateNodeMap(data) {
  if (Array.isArray(data)) {
    data.forEach(updateSingleNode)
  } else {
    updateSingleNode(data)
  }
}

function updateSingleNode(nodeData) {
  const nodeElement = document.getElementById(`node-${nodeData.node_number}`)
  if (nodeElement) {
    nodeElement.dataset.stress = nodeData.estado_estres
    nodeElement.title = `Nodo ${nodeData.node_number}: ${nodeData.estado_estres === 0 ? "Con estrés" : "Sin estrés"}`
  }
}

function showTooltip(event) {
  const node = event.target
  const nodeNumber = node.id.split("-")[1]
  const tooltip = document.createElement("div")
  tooltip.className = "node-tooltip"
  if (node.id === "border-router") {
    tooltip.textContent = `${nodeInfo.router.name}: ${nodeInfo.router.ipv6}`
  } else {
    tooltip.textContent = `${nodeInfo[nodeNumber].name}: ${node.title}`
  }
  document.body.appendChild(tooltip)

  const rect = node.getBoundingClientRect()
  tooltip.style.left = `${rect.left + window.scrollX}px`
  tooltip.style.top = `${rect.bottom + window.scrollY}px`
}

function hideTooltip() {
  const tooltip = document.querySelector(".node-tooltip")
  if (tooltip) {
    tooltip.remove()
  }
}

function updateNodeInfo(data) {
  const nodeInfoBody = document.getElementById("node-info-body")
  nodeInfoBody.innerHTML = ""

  for (let i = 1; i <= 4; i++) {
    const nodeData = Array.isArray(data)
      ? data.find((item) => item.node_number === i)
      : data.node_number === i
        ? data
        : null
    const row = document.createElement("tr")
    row.innerHTML = `
            <td>${i}</td>
            <td>${nodeInfo[i].name}</td>
            <td>${nodeData ? (nodeData.estado_estres === 0 ? "Con estrés" : "Sin estrés") : "N/A"}</td>
            <td>${nodeInfo[i].mac}</td>
            <td>${nodeInfo[i].ipv6}</td>
        `
    nodeInfoBody.appendChild(row)
  }
}

function toggleNodeInfoTable() {
  const table = document.getElementById("node-info-table")
  table.classList.toggle("hidden")
  const button = document.getElementById("toggle-node-info")
  button.textContent = table.classList.contains("hidden")
    ? "Mostrar Información de Nodos"
    : "Ocultar Información de Nodos"
}

// Actuator Control Functions
function toggleActuator() {
  const isChecked = document.getElementById("actuator-toggle").checked
  actuatorState = isChecked ? "on" : "off"

  console.log(`Turning actuator ${actuatorState}`)
  fetch("/actuator", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ action: actuatorState.toUpperCase() }),
  })
    .then((response) => response.json())
    .then((data) => console.log(data))
    .catch((error) => console.error("Error:", error))

  updateRouterColor()
  updateActuatorStatus()
}

function updateActuatorStatus() {
  const statusElement = document.getElementById("actuator-status")
  statusElement.textContent = actuatorState === "on" ? "Encendido" : "Apagado"
  statusElement.style.color = actuatorState === "on" ? "var(--switch-on-color)" : "var(--switch-off-color)"
}

function setActuator() {
  const onTime = document.getElementById("actuator-on-time").value
  const offTime = document.getElementById("actuator-off-time").value
  const days = Array.from(document.querySelectorAll(".actuator-days input:checked")).map((cb) => cb.value)

  if (onTime && offTime) {
    addActuatorToList(onTime, offTime, days)
    saveActuatorSchedule(onTime, offTime, days) // Guardar en localStorage
    document.getElementById("actuator-on-time").value = ""
    document.getElementById("actuator-off-time").value = ""
    document.querySelectorAll(".actuator-days input").forEach((cb) => (cb.checked = false))
    actuatorScheduled = true
    updateRouterColor()
    checkActuators() // Check immediately after setting a new actuator
  } else {
    alert("Por favor, ingrese ambas horas.")
  }
}

function addActuatorToList(onTime, offTime, days) {
  const actuatorList = document.getElementById("actuator-list")
  const actuatorItem = document.createElement("div")
  actuatorItem.className = "actuator-item"
  const daysText =
    days.length > 0 ? days.map((day) => ["Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"][day]).join(", ") : "Una vez"
  actuatorItem.innerHTML = `
        <span>Encender: ${onTime}, Apagar: ${offTime}<br>Días: ${daysText}</span>
        <button class="delete-actuator">Eliminar</button>
    `

  actuatorItem.querySelector(".delete-actuator").addEventListener("click", () => {
    actuatorList.removeChild(actuatorItem)
    removeActuatorFromSchedule(onTime, offTime, days) // Eliminar del localStorage
    if (actuatorList.children.length === 0) {
      actuatorScheduled = false
      updateRouterColor()
    }
  })

  actuatorList.appendChild(actuatorItem)
}

function checkActuators() {
  const now = new Date()
  const currentTime = now.toTimeString().slice(0, 5) // Formato HH:MM
  const currentDay = now.getDay().toString() // Obtener el día actual (0-6)

  console.log(currentDay)

  let shouldBeOn = false

  document.querySelectorAll(".actuator-item").forEach((actuator) => {
    const [onTime, offTime] = actuator.querySelector("span").textContent.match(/\d{2}:\d{2}/g)
    const days = actuator.querySelector("span").textContent.split("Días: ")[1].split(", ")
    const daysMap = { Dom: "0", Lun: "1", Mar: "2", Mié: "3", Jue: "4", Vie: "5", Sáb: "6" }

    // Verificar si la hora actual está entre la hora de encendido y apagado
    if (currentTime >= onTime && currentTime < offTime) {
      // Verificar si el día actual está en la lista de días seleccionados
      if (days.length === 0 || days.map((day) => daysMap[day]).includes(currentDay)) {
        shouldBeOn = true // El actuador debe estar encendido
      }
    }

    // Eliminar actuadores programados una sola vez si la hora de apagado ha pasado
    if (currentTime >= offTime && days.length === 0) {
      actuator.remove() // Eliminar el actuador de la lista
      removeActuatorFromSchedule(onTime, offTime, days) // Eliminar del localStorage
      if (document.querySelectorAll(".actuator-item").length === 0) {
        actuatorScheduled = false // No hay más actuadores programados
      }
    }
  })

  // Encender o apagar el actuador según la lógica
  if (shouldBeOn && actuatorState !== "on") {
    document.getElementById("actuator-toggle").checked = true
    toggleActuator()
  } else if (!shouldBeOn && actuatorState === "on") {
    document.getElementById("actuator-toggle").checked = false
    toggleActuator()
  }

  updateRouterColor() // Actualizar el color del router según el estado del actuador
}

function saveActuatorSchedule(onTime, offTime, days) {
  const schedules = JSON.parse(localStorage.getItem("actuatorSchedules")) || []
  schedules.push({ onTime, offTime, days })
  localStorage.setItem("actuatorSchedules", JSON.stringify(schedules))
}

function loadActuatorSchedule() {
  const schedules = JSON.parse(localStorage.getItem("actuatorSchedules")) || []
  schedules.forEach(({ onTime, offTime, days }) => {
    addActuatorToList(onTime, offTime, days)
  })
}

function removeActuatorFromSchedule(onTime, offTime, days) {
  let schedules = JSON.parse(localStorage.getItem("actuatorSchedules")) || []
  schedules = schedules.filter(
    (schedule) =>
      !(
        schedule.onTime === onTime &&
        schedule.offTime === offTime &&
        JSON.stringify(schedule.days) === JSON.stringify(days)
      ),
  )
  localStorage.setItem("actuatorSchedules", JSON.stringify(schedules))
}

function updateRouterColor() {
  const router = document.getElementById("border-router")
  if (actuatorState === "on") {
    router.className = "router router-on"
  } else if (actuatorScheduled) {
    router.className = "router router-alarm"
  } else {
    router.className = "router router-off"
  }
}

// Initialize router color
updateRouterColor()

function toggleSchedule() {
  const scheduleSection = document.getElementById("actuator-schedule")
  scheduleSection.classList.toggle("hidden")
  updateToggleScheduleButton()
}

function updateToggleScheduleButton() {
  const button = document.getElementById("toggle-schedule")
  const scheduleSection = document.getElementById("actuator-schedule")
  if (scheduleSection.classList.contains("hidden")) {
    button.textContent = "Mostrar Programación"
  } else {
    button.textContent = "Ocultar Programación"
  }
}


