> Исторический backlog (2019). Актуальная спецификация и порядок работ: [README.md](README.md), [07-roadmap.md](07-roadmap.md).  
> Формат карт эволюционирует от [config.json](config.json) к [examples/demo-plant.modbusproj.json](examples/demo-plant.modbusproj.json) — см. [03-modbus-projects.md](03-modbus-projects.md).

1. Реализовать процедуру опроса IoT сенсора посредством протокола MofBud TCP/UDP (в первую оченедь TCP). Для этого, нужно:
	1.1 Получить информацию о том как система должна быть сконфигурированна(карту адресов, адрес устройства, сетевые настройки (уточнить формат карты));
	1.2 Исходя из карты сформировать ряд пакетов ModBus;
	1.3 Применить сетевые настройки;
	1.4 Отправить запрос - получить ответ
	1.5 Обрпботпть ответ
	1.6 Обновить внутренние буферы свежими данными
	1.7 Перейти к этапу 1.4

2. Предоставить внутрипрограммный интерфейс для доступа и работы с получеными данными на шаге 1, выводить эти данные в консоль.



Links:
TCP/UDP protocl:	https://www.techrepublic.com/article/exploring-the-anatomy-of-a-data-packet/
ModBus reference:	http://www.modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf